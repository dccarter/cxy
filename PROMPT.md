Now lets focus on switch statements. Our current syntax requires using a case keyword for each declaration. I think we can simplify things by making the case and default keywords optional. We will allow the following syntax:
```
switch (expression) {
  0 => <statement1>
  1 => <statement2>
  ... => <statement3>
}
```
Where `...` will represent the default case.
- So basically we are just removing the need for the `case` keyword and allowing the default case to be represented by `...`.
- Your role in this is to write tests for the new syntax. Not a lot, just enough to ensure that the parser can handle the new syntax correctly.
- Follow existing test patterns in [@test_for_loop.cpp](file:///Users/dc/projects/cxy/tests/lang/parser/test_for_loop.cpp) 
- Add them a new file `tests/lang/parser/test_switch_statement.cpp`
- The tests will fail but that's ok. We will fix them in the next step.
- After that, you need to analyze the parser and let me know what changes I need to make to support the new syntax.
