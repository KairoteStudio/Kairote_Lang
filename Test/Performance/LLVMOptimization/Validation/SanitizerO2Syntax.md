| 用例 | 特性 | 编译 | 运行 | 结果 | 备注 |
|---|---|---|---|---|---|
| C01_empty |  | ✅ | ✅ | PASS | |
| C02_retcode |  | ✅ | ✅ | PASS | exit=7 |
| C03_comments | 行注释 | ✅ | ✅ | PASS | |
| C10_var_infer |  | ✅ | ✅ | PASS | |
| C11_inttypes |  | ✅ | ✅ | PASS | |
| C12_uinttypes |  | ✅ | ✅ | PASS | |
| C13_boolchar |  | ✅ | ✅ | PASS | |
| C14_streq |  | ✅ | ✅ | PASS | |
| C16_const |  | ✅ | ✅ | PASS | |
| C17_global |  | ✅ | ✅ | PASS | |
| C18_uninit |  | ✅ | ✅ | PASS | |
| C20_prec |  | ✅ | ✅ | PASS | |
| C21_divmod |  | ✅ | ✅ | PASS | |
| C22_compound |  | ✅ | ✅ | PASS | |
| C23_incdec |  | ✅ | ✅ | PASS | |
| C24_float |  | ✅ | ✅ | PASS | |
| C25_cmp |  | ✅ | ✅ | PASS | |
| C26_andor |  | ✅ | ✅ | PASS | |
| C27_ternary |  | ✅ | ✅ | PASS | |
| C28_bitwise |  | ✅ | ✅ | PASS | |
| C29_shortcircuit |  | ✅ | ✅ | PASS | |
| C30_while |  | ✅ | ✅ | PASS | |
| C31_break |  | ✅ | ✅ | PASS | |
| C32_continue |  | ✅ | ✅ | PASS | |
| C33_dowhile |  | ✅ | ✅ | PASS | |
| C34_for_decl |  | ✅ | ✅ | PASS | |
| C35_for_outer |  | ✅ | ✅ | PASS | |
| C36_nested |  | ✅ | ✅ | PASS | |
| C37_switch |  | ✅ | ✅ | PASS | |
| C38_earlyret |  | ✅ | ✅ | PASS | |
| C39_scope |  | ✅ | ✅ | PASS | |
| C40_fib |  | ✅ | ✅ | PASS | |
| C41_manyargs |  | ✅ | ✅ | PASS | |
| C42_forward |  | ✅ | ✅ | PASS | |
| C43_boolret |  | ✅ | ✅ | PASS | |
| C50_arrconst |  | ✅ | ✅ | PASS | |
| C51_arrvaridx |  | ✅ | ✅ | PASS | |
| C52_newarray |  | ✅ | ✅ | PASS | |
| C53_arrfill |  | ✅ | ✅ | PASS | |
| C54_strconst |  | ✅ | ✅ | PASS | |
| C55_strconcat |  | ✅ | ✅ | PASS | |
| C56_strescape |  | ✅ | ✅ | PASS | |
| C60_staticcall |  | ✅ | ✅ | PASS | |
| C61_instance |  | ✅ | ✅ | PASS | |
| C62_ctor |  | ✅ | ✅ | PASS | |
| C63_staticfield |  | ✅ | ✅ | PASS | |
| C64_inherit |  | ✅ | ✅ | PASS | |
| C65_this |  | ✅ | ✅ | PASS | |
| C66_interface |  | ✅ | ✅ | PASS | |
| C67_enum |  | ✅ | ✅ | PASS | |
| C68_struct |  | ✅ | ✅ | PASS | |
| C69_generic |  | ✅ | ✅ | PASS | |
| C70_trycatch |  | ✅ | ✅ | PASS | |
| C71_foreach |  | ✅ | ✅ | PASS | |
| C72_lambda |  | ✅ | ✅ | PASS | |
| C73_namespace |  | ✅ | ✅ | PASS | |
| C74_null |  | ✅ | ✅ | PASS | |
| C75_refparam |  | ✅ | ✅ | PASS | |
| C76_casts |  | ✅ | ✅ | PASS | |
| C77_charrw |  | ✅ | ✅ | PASS | |
| C78_intvaridx2 |  | ✅ | ✅ | PASS | |
| C79_strlenwalk |  | ✅ | ✅ | PASS | |
| C80_unsafe_pointers | requested example | ✅ | ✅ | PASS | exit=0 |
| C81_pointer_increment | memory lvalue increment | ✅ | ✅ | PASS | exit=0 |
| C82_pointer_evaluation | pointer operand evaluation order | ✅ | ✅ | PASS | exit=0 |
| C83_pointer_ternary | pointer ternary and short circuit | ✅ | ✅ | PASS | exit=0 |
| C84_pointer_nullable | nullable pattern scopes and single evaluation | ✅ | ✅ | PASS | exit=0 |
| C85_pointer_callback | function pointer signature stack and wide arguments | ✅ | ✅ | PASS | exit=0 |
| C86_pointer_stack | stack pages calls and multiple allocations | ✅ | ✅ | PASS | exit=0 |
| C87_pointer_float | float pointer operations aliases and calls | ✅ | ✅ | PASS | exit=0 |
| C88_pointer_shadow | narrow binding restores shadowed pointer | ✅ | ✅ | PASS | exit=0 |
| C89_pointer_compound | memory lvalue compound pointer | ✅ | ✅ | PASS | exit=0 |
| C90_pointer_arguments | call argument evaluation order | ✅ | ✅ | PASS | exit=0 |
| C91_pointer_pressure | addressed locals under register pressure | ✅ | ✅ | PASS | exit=0 |
| M1_main |  | ✅ | ✅ | PASS | |

**统计**: PASS=75, 输出错误=0, 编译失败=0, 运行崩溃=0, 总计=75
