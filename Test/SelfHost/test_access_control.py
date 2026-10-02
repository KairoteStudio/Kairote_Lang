"""Access contracts run the real native CLI with no tools available on PATH."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('SELFHOST_COMPILER', ROOT / 'build/selfhost/stage2/program')).resolve()
LINKER = Path(os.environ.get('ARKLINK', ROOT / 'build/ArkLink/ArkLink')).resolve()


@unittest.skipUnless(COMPILER.is_file() and LINKER.is_file(), 'build the native compiler and ArkLink first')
class AccessControlTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='krt-access-')
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.env = dict(os.environ, PATH='')
        self.counter = 0

    def write(self, name, text):
        path = self.work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def command(self, *arguments, expected=0):
        result = subprocess.run([str(COMPILER), '--linker', str(LINKER), *map(str, arguments)],
                                cwd=self.work, env=self.env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def execute(self, source, *, files=None, extra=(), vm=False):
        self.counter += 1
        prefix = f'positive-{self.counter}'
        path = self.write(prefix + '.krt', source)
        roots = [path]
        for name, text in (files or {}).items():
            roots.append(self.write(name, text))
        output = self.work / prefix
        arguments = [*roots, '-O2', '-o', output, *extra]
        if vm:
            arguments += ['target', 'vm']
        self.command(*arguments)
        argv = [str(COMPILER), 'run-vm', str(output)] if vm else [str(output)]
        result = subprocess.run(argv, cwd=self.work, env=self.env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, source + '\n' + result.stdout + result.stderr)

    def reject(self, source, *, code='E_ACCESS', label=None, files=None, extra=(), vm=False):
        self.counter += 1
        path = self.write(f'negative-{self.counter}.krt', source)
        roots = [path]
        for name, text in (files or {}).items():
            roots.append(self.write(name, text))
        output = self.work / f'existing-{self.counter}'
        previous = b'previous valid artifact\x00must survive'
        output.write_bytes(previous)
        arguments = [*roots, '-O2', '-o', output, *extra]
        if vm:
            arguments += ['target', 'vm']
        result = self.command(*arguments, expected=1)
        self.assertIn(code, result.stderr)
        self.assertNotIn('E_PARSE', result.stderr, result.stderr)
        self.assertEqual(output.read_bytes(), previous)
        if code != 'E_LINK':
            self.assertRegex(result.stderr, re.escape(str(path)) + r':\d+:\d+:')
        if label:
            offset = source.rindex(label)
            line = source[:offset].count('\n') + 1
            column = len(source[:offset].rsplit('\n', 1)[-1]) + 1
            self.assertIn(f'{path}:{line}:{column}:', result.stderr)
        return result, path

    def test_default_access_remains_public_for_existing_programs(self):
        self.execute('''
class Box{int32 value=17;int32 Read(){return value;}static int32 Add(int32 n){return n+1;}}
int32 answer(){return 23;}
int32 main(){Box box=new Box();if(box.value!=17||box.Read()!=17||Box.Add(answer())!=24){return 1;}delete box;return 0;}
''')

    def test_private_members_allow_same_owner_other_instances_and_private_calls(self):
        self.execute('''
class Box{private int32 value;private static int32 offset=2;
private int32 Sum(int32 n){return value+n+offset;}
public Box(int32 n){value=n;}
public int32 Peek(Box other){return other.value+other.Sum(1);}}
int32 main(){Box left=new Box(3);Box right=new Box(17);if(left.Peek(right)!=37){return 1;}delete left;delete right;return 0;}
''')

    def test_private_fields_reject_read_write_update_and_ref(self):
        declarations='class Box{private int32 secret=7;}void Set(ref int32 n){n=42;}'
        for body in ('return box.secret;', 'box.secret=42;return 0;', 'box.secret+=1;return 0;',
                     'box.secret++;return 0;', 'Set(ref box.secret);return 0;'):
            with self.subTest(body=body):
                self.reject(declarations + 'int32 main(){Box box=new Box();' + body + '}', label='secret')

    def test_private_instance_static_and_inherited_methods_are_rejected(self):
        self.reject('class Box{private int32 Read(){return 7;}}int32 main(){Box box=new Box();return box.Read();}', label='Read')
        self.reject('class Box{private static int32 Read(){return 7;}}int32 main(){return Box.Read();}', label='Read')
        self.reject('class Parent{private int32 Read(){return 7;}}class Child:Parent{public int32 Call(){return Read();}}int32 main(){return 0;}', label='Read')

    def test_private_field_addresses_static_storage_and_constants_require_access(self):
        for operand in ('secret', 'this.secret'):
            with self.subTest(operand=operand):
                self.execute('''
class Box{private int32 secret=42;public int32 Read(){unsafe(using krt.mem;){int32* pointer=&OPERAND;return *pointer;}}}
int32 main(){Box item=new Box();if(item.Read()!=42){return 1;}delete item;return 0;}
'''.replace('OPERAND', operand))
        for value_type, initial, updated in (
                ('int8','-7','-9'),('uint16','60000','50000'),
                ('int64','4294967313','4294967321'),('float32','1.5','2.25'),
                ('uint128','((uint128)1<<96)+17','((uint128)1<<100)+25')):
            for operand in ('secret','this.secret'):
                with self.subTest(value_type=value_type,operand=operand):
                    self.execute(f'''
class Box{{private {value_type} secret={initial};public bool Change(){{unsafe(using krt.mem;){{
{value_type}* pointer=&{operand};if(*pointer!={initial}){{return false;}}*pointer={updated};return secret=={updated};}}}}}}
int32 main(){{Box item=new Box();if(!item.Change()){{return 1;}}delete item;return 0;}}
''')
        self.reject('class Box{private int32 secret=42;}int32 main(){unsafe(using krt.mem;){Box item=new Box();int32* pointer=&item.secret;return *pointer;}}',label='secret')
        self.reject('class Box{private static int32 secret=42;}int32 main(){Box.secret=17;return 0;}',label='secret')
        self.reject('class Box{private const int32 secret=42;}int32 main(){return Box.secret;}',label='secret')

    def test_protected_members_allow_this_base_and_derived_receivers(self):
        self.execute('''
class Parent{protected int32 value=17;protected int32 Add(int32 n){return value+n;}
protected static int32 Bonus(){return 3;}}
class Child:Parent{public int32 Total(Child other){return this.value+base.Add(2)+other.value+Child.Bonus();}}
int32 main(){Child left=new Child();Child right=new Child();if(left.Total(right)!=56){return 1;}delete left;delete right;return 0;}
''')

    def test_protected_receivers_reject_parent_and_sibling_static_types(self):
        common='class Parent{protected int32 value=17;}class Other:Parent{}'
        for parameter in ('Parent', 'Other'):
            with self.subTest(receiver=parameter):
                self.reject(common + f'class Child:Parent{{public int32 Read({parameter} other){{return other.value;}}}}int32 main(){{return 0;}}', label='value')
        self.reject(common + 'int32 main(){Parent value=new Parent();return value.value;}', label='value')

    def test_protected_internal_or_and_private_protected_and(self):
        self.execute('''
class Parent{protected internal int32 shared=17;private protected int32 limited=25;}
class Child:Parent{public int32 Read(){return limited;}}
int32 main(){Parent parent=new Parent();Child child=new Child();if(parent.shared+child.Read()!=42){return 1;}delete parent;delete child;return 0;}
''')
        self.reject('class Parent{private protected int32 limited=42;}int32 main(){Parent item=new Parent();return item.limited;}', label='limited')

    def test_static_and_constructor_access_and_private_factory(self):
        self.execute('''
class Vault{private int32 value;private Vault(int32 n){value=n;}
public static Vault Create(){return new Vault(42);}public int32 Read(){return value;}}
class Parent{protected Parent(){}}
class Child:Parent{public Child():base(){}}
int32 main(){Vault value=Vault.Create();Child child=new Child();if(value.Read()!=42){return 1;}delete value;delete child;return 0;}
''')
        self.reject('class Vault{private Vault(){}}int32 main(){Vault value=new Vault();return 0;}')
        self.reject('class Parent{protected Parent(){}}int32 main(){Parent value=new Parent();return 0;}')
        self.reject('class Parent{private Parent(){}}class Child:Parent{public Child():base(){}}int32 main(){return 0;}')

    def test_new_constraint_requires_public_or_default_public_constructor(self):
        self.execute('''
class Item{public int32 value=42;}
T create<T>() where T:new(){return new T();}
int32 main(){Item item=create<Item>();if(item.value!=42){return 1;}delete item;return 0;}
''')
        self.reject('class Item{private Item(){}}T create<T>() where T:new(){return new T();}int32 main(){create<Item>();return 0;}', code='E_GENERIC_CONSTRAINT')
        self.reject('class Item{protected Item(){}}T create<T>() where T:new(){return new T();}int32 main(){create<Item>();return 0;}', code='E_GENERIC_CONSTRAINT')
        self.reject('class Item{internal Item(){}}T create<T>() where T:new(){return new T();}int32 main(){create<Item>();return 0;}', code='E_GENERIC_CONSTRAINT')

    def test_generics_preserve_private_access_and_canonical_owner(self):
        self.execute('''
class Box<T>{private int32 marker;public Box(int32 n){marker=n;}
public int32 Peek(Box<int64> other){return other.marker;}}
int32 main(){Box<int32> left=new Box<int32>(7);Box<int64> right=new Box<int64>(42);
if(left.Peek(right)!=42){return 1;}delete left;delete right;return 0;}
''')
        self.reject('class Box<T>{private T value;public Box(T n){value=n;}}int32 main(){Box<int32> box=new Box<int32>(42);return box.value;}', label='value')
        self.reject('class Parent<T>{private T value;}class Child<T>:Parent<T>{public T Read(){return value;}}int32 main(){Child<int32> item=new Child<int32>();return item.Read();}')

    def test_protected_generic_base_keeps_receiver_rules(self):
        self.execute('''
class Parent<T>{protected T value;protected Parent(T n){value=n;}}
class Child<T>:Parent<T>{public Child(T n):base(n){}public T Read(){return base.value;}}
int32 main(){Child<int32> item=new Child<int32>(42);if(item.Read()!=42){return 1;}delete item;return 0;}
''')
        self.reject('class Parent<T>{protected T value;}class Child<T>:Parent<T>{public T Read(Parent<T> item){return item.value;}}int32 main(){Child<int32> item=new Child<int32>();return item.Read(item);}', label='value')

    def test_accessible_overloads_are_selected_without_private_best_match(self):
        self.execute('''
class Choice{private int32 Pick(int32 n){return 1;}public int32 Pick(int64 n){return 42;}}
int32 main(){Choice item=new Choice();if(item.Pick((int32)7)!=42){return 1;}delete item;return 0;}
''')
        self.reject('class Choice{private int32 Pick(int32 n){return n;}}int32 main(){Choice item=new Choice();return item.Pick(42);}', label='Pick')

    def test_inaccessible_hidden_field_does_not_fall_back_to_base_field(self):
        self.execute('''
class Parent{public int32 value=42;}class Child:Parent{private int32 value=7;public int32 Own(){return value;}}
int32 main(){Child item=new Child();Parent view=item;if(item.Own()!=7||view.value!=42){return 1;}delete item;return 0;}
''')
        self.reject('class Parent{public int32 value=42;}class Child:Parent{private int32 value=7;}int32 main(){Child item=new Child();return item.value;}', label='value')

    def test_function_addresses_check_access_before_indirect_calls(self):
        self.execute('''
class Ops{private static int32 Secret(int32 n){return n+1;}
public static int32 Run(){fn(int32)->int32 callback=&Ops.Secret;return callback(41);}}
int32 main(){return Ops.Run()==42?0:1;}
''')
        self.reject('class Ops{private static int32 Secret(int32 n){return n;}}int32 main(){fn(int32)->int32 callback=&Ops.Secret;return callback(42);}', label='Secret')

    def test_override_visibility_and_interface_implementation_contracts(self):
        self.execute('''
interface I{int32 Read();}class Parent{protected virtual int32 Hidden(){return 1;}}
class Child:Parent,I{protected override int32 Hidden(){return 42;}public int32 Read(){return Hidden();}}
int32 main(){Child item=new Child();I view=item;if(view.Read()!=42){return 1;}delete item;return 0;}
''')
        self.execute('''
class Parent{virtual int32 Read(){return 1;}}class Child:Parent{public override int32 Read(){return 42;}}
int32 main(){Child item=new Child();Parent view=item;if(view.Read()!=42){return 1;}delete item;return 0;}
''')
        self.reject('class Parent{public virtual int32 Read(){return 1;}}class Child:Parent{private override int32 Read(){return 42;}}int32 main(){return 0;}', code='E_MODIFIER')
        self.reject('class Parent{protected virtual int32 Read(){return 1;}}class Child:Parent{public override int32 Read(){return 42;}}int32 main(){return 0;}', code='E_MODIFIER')
        self.reject('interface I{int32 Read();}class Item:I{private int32 Read(){return 42;}}int32 main(){return 0;}', code='E_MODIFIER')

    def test_internal_and_default_public_are_visible_across_original_roots(self):
        self.execute('''
int32 main(){Package item=new Package();if(item.value+PublicValue()!=42){return 1;}delete item;return 0;}
''', files={'package.krt': 'internal class Package{internal int32 value=17;}int32 PublicValue(){return 25;}'})

    def test_internal_is_visible_through_imports_in_the_same_assembly(self):
        self.write('modules/Package/Api.krt', 'namespace Package;internal class Api{internal static int32 Value(){return 42;}}')
        self.execute('using Package.Api;int32 main(){return Api.Value()==42?0:1;}', extra=('-I', self.work / 'modules'))

    def test_top_level_private_is_original_file_local(self):
        self.execute('''
private class Hidden{public int32 value=42;}
private int32 local(){Hidden item=new Hidden();int32 n=item.value;delete item;return n;}
int32 Read(){return local();}int32 main(){return Read()==42?0:1;}
''')
        self.reject('int32 main(){return hidden();}', files={'helper.krt': 'private int32 hidden(){return 42;}'}, label='hidden')
        self.reject('int32 main(){return hidden;}', files={'data.krt': 'private static int32 hidden=42;'}, label='hidden')
        self.reject('int32 main(){Hidden item=new Hidden();return 0;}', files={'type.krt': 'private class Hidden{}'})

    def test_quoted_import_does_not_expose_file_private_declarations(self):
        self.write('shared/hidden.krt', 'private int32 hidden(){return 42;}public int32 Read(){return hidden();}')
        self.execute('import "shared/hidden.krt";int32 main(){return Read()==42?0:1;}')
        self.reject('import "shared/hidden.krt";int32 main(){return hidden();}', label='hidden')

    def test_file_private_same_namespace_names_bind_to_their_own_files(self):
        for declaration, expression in (
                ('private int32 local(){return VALUE;}', 'local()'),
                ('private static int32 local=VALUE;', 'local'),
                ('private const int32 local=VALUE;', 'local'),
                ('private class Local{public int32 value=VALUE;}', 'new Local().value')):
            with self.subTest(declaration=declaration):
                a='namespace Shared;' + declaration.replace('VALUE','17') + 'public int32 FromA(){return ' + expression + ';}'
                b='namespace Shared;' + declaration.replace('VALUE','25') + 'public int32 FromB(){return ' + expression + ';}'
                self.execute('int32 main(){return Shared.FromA()+Shared.FromB()==42?0:1;}',
                             files={f'a-{self.counter}.krt':a,f'b-{self.counter}.krt':b})

    def test_file_private_and_public_names_coexist_with_private_local_priority(self):
        cases = (
            ('class', {'private.krt':'namespace Shared;private class Item{public int32 value=17;}public int32 FromA(){return new Item().value;}',
                       'public.krt':'namespace Shared;public class Item{public int32 value=25;}public int32 FromB(){return new Item().value;}'}),
            ('function', {'private-function.krt':'namespace Shared;private int32 local(){return 17;}public int32 FromA(){return local();}',
                          'public-function.krt':'namespace Shared;public int32 local(){return 25;}public int32 FromB(){return local();}'}))
        for declaration, files in cases:
            with self.subTest(declaration=declaration):
                self.execute('int32 main(){return Shared.FromA()+Shared.FromB()==42?0:1;}',files=files)

    def test_same_file_duplicate_private_declarations_still_reject(self):
        for declarations in ('private int32 local(){return 1;}private int32 local(){return 2;}',
                             'private static int32 local=1;private static int32 local=2;',
                             'private const int32 local=1;private const int32 local=2;',
                             'private class Local{}private class Local{}'):
            with self.subTest(declarations=declarations):
                result,_=self.reject(declarations + 'int32 main(){return 0;}',code='E_')
                self.assertRegex(result.stderr,r'E_(?:MODULE|LOWER)\b')

    def test_cross_file_extern_prototype_does_not_bind_private_definition(self):
        self.reject('extern int32 hidden();int32 main(){return hidden();}',
                    code='E_LINK',files={'private-definition.krt':'private int32 hidden(){return 42;}'})
        library=self.write('public-external.krt','public int32 hidden(){return 25;}')
        object_path=self.work/'public-external.kro'
        self.command(library,'-c','-o',object_path)
        self.execute('extern int32 hidden();int32 main(){return hidden()+ReadPrivate()==42?0:1;}',
                     files={'private-helper.krt':'private int32 hidden(){return 17;}public int32 ReadPrivate(){return hidden();}'},
                     extra=(object_path,))

    def test_null_actual_does_not_bypass_private_formal_type_access(self):
        self.reject('int32 main(){return Consume(null);}',files={
            'api.krt':'private class Hidden{}public int32 Consume(Hidden value){return 42;}'},label='Consume')

    def test_private_return_types_cannot_escape_access_checks_through_inference(self):
        api='private class Hidden{public int32 Read(){return 42;}}public Hidden Make(){return new Hidden();}'
        self.execute(api + 'public fn()->Hidden GetFactory(){return &Make;}' +
                     'int32 main(){var factory=GetFactory();Hidden item=factory();' +
                     'if(item.Read()!=42){return 1;}delete item;return 0;}')
        for body in ('var item=Make();return item.Read();', 'return Make().Read();'):
            with self.subTest(body=body):
                self.reject('int32 main(){' + body + '}', files={'api.krt': api})
        self.reject('int32 main(){var items=MakeArray();return items[0].Read();}', files={
            'api.krt': 'private class Hidden{public int32 Read(){return 42;}}public Hidden[] MakeArray(){return [new Hidden()];}'})
        self.reject('int32 main(){var factory=GetFactory();var item=factory();return item.Read();}', files={
            'api.krt': api + 'public fn()->Hidden GetFactory(){return &Make;}'})

    def test_private_and_internal_are_not_exported_from_separate_kro(self):
        library=self.write('library.krt', 'public int32 visible(){return 42;}private int32 hidden(){return 7;}internal int32 package(){return 9;}')
        object_path=self.work/'library.kro'
        self.command(library, '-c', '-o', object_path)
        self.execute('extern int32 visible();int32 main(){return visible()==42?0:1;}', extra=(object_path,))
        for name in ('hidden', 'package'):
            with self.subTest(symbol=name):
                self.reject(f'extern int32 {name}();int32 main(){{return {name}();}}', code='E_LINK', extra=(object_path,))

    def test_conflicting_duplicate_and_invalid_visibility_modifiers(self):
        for declaration in (
                'class Bad{public private int32 value;}',
                'class Bad{public public int32 value;}',
                'class Bad{private internal int32 value;}',
                'class Bad{public protected int32 Read(){return 1;}}',
                'class Bad{private virtual int32 Read(){return 1;}}',
                'abstract class Bad{private abstract int32 Read();}',
                'protected class Bad{}',
                'private protected class Bad{}',
                'protected int32 global(){return 1;}',
                'class Bad{protected internal private int32 value;}'):
            with self.subTest(declaration=declaration):
                self.reject(declaration + 'int32 main(){return 0;}', code='E_MODIFIER')

    def test_independent_access_errors_all_report_original_positions(self):
        source='''
class Vault{private int32 secret=7;private int32 Read(){return 7;}private static int32 Hidden(){return 7;}}
int32 First(Vault value){return value.secret;}
int32 Second(Vault value){return value.Read();}
int32 Third(){return Vault.Hidden();}
int32 main(){return 0;}
'''
        result,path=self.reject(source)
        diagnostics=[line for line in result.stderr.splitlines() if 'E_ACCESS' in line]
        self.assertEqual(len(diagnostics),3,result.stderr)
        for label in ('secret;', 'Read();', 'Hidden();'):
            offset=source.rindex(label)
            line=source[:offset].count('\n')+1
            column=len(source[:offset].rsplit('\n',1)[-1])+1
            self.assertTrue(any(f'{path}:{line}:{column}:' in diagnostic for diagnostic in diagnostics),result.stderr)

    def test_native_and_vm_apply_the_same_access_rules(self):
        source='''
class Parent{protected int32 value=42;}class Child:Parent{private int32 Read(){return value;}public int32 Run(){return Read();}}
int32 main(){Child item=new Child();if(item.Run()!=42){return 1;}delete item;return 0;}
'''
        for vm in (False,True):
            with self.subTest(vm=vm):
                self.execute(source,vm=vm)
                self.reject('class Box{private int32 secret=42;}int32 main(){Box item=new Box();return item.secret;}',label='secret',vm=vm)
                self.reject('class Box{public private int32 value;}int32 main(){return 0;}',code='E_MODIFIER',vm=vm)

    def test_existing_library_private_helpers_remain_callable_inside_owner(self):
        self.execute('''
using System.Array;using System.Console;using System.Math;
int32 main(){int32[] values=[1,2,3];ArrayOps.Reverse(values);if(values[0]!=3||MathOps.GCD((int64)42,(int64)30)!=6){return 1;}
Console.Write("");delete values;return 0;}
''')
        self.reject('using System.Console;int32 main(){Console.write_raw(1,0,0);return 0;}',label='write_raw')
