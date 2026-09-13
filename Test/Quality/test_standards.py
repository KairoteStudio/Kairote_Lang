"""Regression checks for declaration-aware DevStand auditing."""
import unittest

if __package__:
    from . import CheckStandards as standards
else:
    import CheckStandards as standards


def header(body):
    return "#ifndef KRT_TEST_H\n#define KRT_TEST_H\n" + body + "\n#endif\n"


class StandardsChecks(unittest.TestCase):
    def check_rules(self, source, baseline="", suffix=".h"):
        path = standards.ROOT / ("Re.KrtC/src/StandardsFixture" + suffix)
        return standards.check_rules(path, source, baseline)

    def test_comments_strings_and_continued_macros_are_not_functions(self):
        source = (
            '#define KRT_FAKE() \\\n    static int BadMacro(void) { return 0; }\n'
            '// ignored \\\nstatic int BadComment(void) { return 0; }\n'
            'const char* text = "ignored \\\nstatic int BadString(void) { return 0; }";\n'
            '/* static int BadBlock(void) { return 0; } */\n'
            'static int safe_helper(void) {\n    return 0;\n}\n'
        )
        functions = list(standards.functions(source))
        self.assertEqual([item[0] for item in functions], ["safe_helper"])
        self.assertEqual(functions[0][3], 3)

    def test_leading_and_trailing_attributes_preserve_the_function_name(self):
        source = (
            'static KRT_PRINTF_FORMAT(2, 0) int format_output(int mode, const char* text) {\n'
            '    return mode;\n}\n'
            '__attribute__((used)) static int another_helper(void) { return 1; }\n'
            'static int annotated(int unused __attribute__((unused))) KRT_NOINLINE { return 2; }\n'
        )
        functions = list(standards.functions(source))
        self.assertEqual([item[0] for item in functions], ["format_output", "another_helper", "annotated"])
        self.assertTrue(all("static" in item[1] for item in functions))
        self.assertEqual(self.check_rules(source, suffix=".c"), ([], []))

    def test_header_inline_calls_and_struct_fields_are_not_public_declarations(self):
        source = header('''
#ifdef __cplusplus
extern "C" {
#endif
/** @brief Forward one value. */
static inline int KrtWrapper(int value) {
    return KrtCalledInside(value);
}
typedef struct {
    int (*KrtCallbackField)(int);
} KrtCallbacks;
/** @brief Print a value. */
void KrtPublic(const char* text, ...) KRT_PRINTF_FORMAT(1, 2);
#ifdef __cplusplus
}
#endif
''')
        self.assertEqual([item.name for item in standards.declarations(source)], ["KrtWrapper", "KrtPublic"])
        self.assertEqual(self.check_rules(source), ([], []))

    def test_new_public_names_are_checked_without_breaking_existing_names(self):
        baseline = header("int legacy_api(void);\n")
        source = header("int legacy_api(void);\n/** @brief New entry. */\nint bad_api(void);\n")
        findings, notices = self.check_rules(source, baseline)
        self.assertEqual(len(findings), 1)
        self.assertIn("new API bad_api must use Krt-prefixed PascalCase", findings[0])
        self.assertEqual(notices, [])

    def test_an_old_call_does_not_exempt_a_new_undocumented_api(self):
        baseline = header('''
/** @brief Wrap a call. */
static inline int KrtWrapper(void) {
    return KrtNewApi();
}
''')
        source = baseline.replace("\n#endif", "\nint KrtNewApi(void);\n#endif")
        findings, _ = self.check_rules(source, baseline)
        self.assertEqual(len(findings), 1)
        self.assertIn("new API KrtNewApi needs a preceding documentation comment", findings[0])

    def test_arklink_api_prefix_is_preserved(self):
        source = header("/** @brief Link the image. */\nint ark_backend_link(void);\n")
        path = standards.ROOT / "ArkLink/include/ArkLink/StandardsFixture.h"
        self.assertEqual(standards.check_rules(path, source, ""), ([], []))

    def test_documentation_must_be_a_documentation_comment(self):
        source = header('''
/* An ordinary implementation note. */
int KrtUndocumented(void);
/** @brief Document an attributed API. */
KRT_PRINTF_FORMAT(1, 2)
int KrtDocumented(const char* text, ...);
/// Return the result.
int KrtLineDocumented(void);
''')
        findings, _ = self.check_rules(source)
        self.assertEqual(len(findings), 1)
        self.assertIn("KrtUndocumented needs", findings[0])

    def test_static_names_are_checked_in_headers_and_existing_sources(self):
        source = header('''
static int BadHelper(void);
static inline int anotherBadHelper(void) { return 0; }
static inline int good_helper(void) { return 0; }
''')
        findings, _ = self.check_rules(source, source)
        self.assertEqual(len(findings), 2)
        self.assertTrue(all("internal function" in finding for finding in findings))
        implementation = "static int KrtPrivate(void) { return 0; }\n"
        findings, _ = self.check_rules(implementation, implementation, ".c")
        self.assertEqual(len(findings), 1)
        self.assertIn("internal function KrtPrivate", findings[0])

    def test_macro_audit_checks_new_names_and_ignores_comment_examples(self):
        baseline = "#define LEGACY 1\n"
        source = baseline + '''
/* #define COMMENTED 1 */
#define KRT_GOOD 2
#define bad_macro 3
#define KRT_Bad 4
'''
        findings, _ = self.check_rules(source, baseline, ".c")
        self.assertEqual(len(findings), 2)
        self.assertTrue(any("new macro bad_macro" in finding for finding in findings))
        self.assertTrue(any("new macro KRT_Bad" in finding for finding in findings))

    def test_preexisting_long_functions_are_reported_and_new_ones_fail(self):
        body = "static int large_function(void) {\n" + "    /* } { */\n" * 500 + "    return 0;\n}\n"
        findings, reports = self.check_rules(body, body, ".c")
        self.assertEqual(findings, [])
        self.assertEqual(len(reports), 1)
        self.assertIn("503 lines (pre-existing", reports[0])
        findings, reports = self.check_rules(body, suffix=".c")
        self.assertEqual(len(findings), 1)
        self.assertIn("503 lines; split or report", findings[0])
        self.assertEqual(reports, [])

    def test_function_pointer_returns_are_apis_but_callback_variables_are_not(self):
        source = header('''
/** @brief Select a callback. */
int (*KrtSelect(void))(int);
int (*callback_variable)(int);
typedef int (*KrtCallback)(int);
static int (*choose_callback(void))(int) { return callback_variable; }
''')
        self.assertEqual([item.name for item in standards.declarations(source)], ["KrtSelect", "choose_callback"])
        self.assertEqual(self.check_rules(source), ([], []))

    def test_commented_include_guards_do_not_satisfy_the_rule(self):
        findings, _ = self.check_rules("/* #ifndef KRT_TEST_H\n#define KRT_TEST_H\n#endif */\n")
        self.assertEqual(len(findings), 1)
        self.assertIn("missing matching include guard", findings[0])
        self.assertEqual(self.check_rules("#if !defined(KRT_TEST_H)\n#define KRT_TEST_H\n#endif\n"), ([], []))
        self.assertEqual(self.check_rules("#ifndef KRT_TEST_H\n/* note */\n#define KRT_TEST_H\n#endif\n"), ([], []))


if __name__ == "__main__":
    unittest.main()
