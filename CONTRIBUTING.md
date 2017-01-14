# Introduction

The Sequence Toolkit is designed by developers, for developers and as such contributiosn are welcome.

Please use project issues to document ideas and/or bugs and 
create pull requests as you see fit!

# Guidelines

## C Code Style

This project uses some less popular conventions in its code style, but pull requests will not be accepted
unless existing conventions are matched. Here are some guiding principles:

* Use lowercase and underscores ('_') to create function/variable names - no Camel case in this project!
* Single line 'if' bodies without { } are encouraged.
* Use do { } while(0) in macros for anything that is more than a single statement macro.
* Use sub-blocks to group variables and logic together. This encourages efficient heap usage,
for example in switch statements.
* Following existing style in a file. File consistency is important for readability.

## Testing

Run tests before creating PRs!

Pull requests with new capabilities will not be accepted without new tests.

Add tests to the test_programs or unit_tests dirs. The difference between the two sets of tests
are unit_tests may access private headers for the purposes of testing. test_programs should be module level
component/interface tests.

