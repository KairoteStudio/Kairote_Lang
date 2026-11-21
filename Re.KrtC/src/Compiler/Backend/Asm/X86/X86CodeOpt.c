/*
 * X86 peephole optimizer removed.
 *
 * The peephole pass had no caller anywhere in the project and its pattern
 * rules were incorrect (audit items 12 and 89): use-count was not validated
 * before rewriting instructions, and several matches ignored operand types.
 * Reintroduce a peephole pass only together with a use-count assertion and
 * regression tests.
 */
