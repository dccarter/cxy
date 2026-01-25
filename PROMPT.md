Okay, those changes worked, now lets also look into our for-loop parsing.
- Lets start making sure we can `in` alongside the `:` on the header
- Lets make the `var` or `const` keyword optional
 - if not provided if we are comptime, then it will default to const, otherwise it will be var
- Your role is to analyze what needs to be done and add the test cases for the feature in tests/lang/parser/test_for_loop.cpp
  - Please follow existing conventions and guidelines for writing test cases.
  - Assert on the expected SExpression using `REQUIRE_AST_MATCHES_IGNORE_METADATA` where possible.
- Do not implement the functionality, just write the test cases. They will fail and that is expected.
- After writing the test cases, you should let me know what changes need to be made to the parser and I will implement them myself.
