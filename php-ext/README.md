# PHP MAPI Extension

This directory contains the php-mapi extension code.

## Testing


PHP's 'run-tests.php' is used for running unittests against the php-mapi module.
Writing php tests is documented on phpinternalsbook.com

http://www.phpinternalsbook.com/tests/phpt_file_structure.html

Generating junit output for Jenkins:

```
TEST_PHP_JUNIT=php-mapi.xml ....
```

Running unit tests:

```
make test
```

Running tests against a Kopano Server requires one environment variable to be set:

```
KOPANO_TEST_SERVER = http://localhost:236/kopano
```

The test username and password defaults to user1 and can be set with an environment variable:

```
KOPANO_TEST_USER=john
KOPANO_TEST_SERVER=pass
```

## Notes

run-tests.php is vendored from PHP itself and can't be run as a normal user
from a PHP installation due to the usage of __DIR__.

## Coverage

Compile with core with profiling instructions:

```
CXXFLAGS+="-fprofile-arcs -ftest-coverage"
```

Then run the test suite and generate coverage with gcovr:

```
gcovr -r . --html --html-details -o php-ext-coverage.html
```
