#!/usr/bin/env bats

@test "list rules" {
  run kopano-ibrule -u ${KOPANO_TEST_USER} -S
  [ "$status" -eq 0 ]
}

@test "Add new rule" {
  run kopano-ibrule -u ${KOPANO_TEST_USER} -A
  [ "$status" -eq 0 ]

  run kopano-ibrule -u ${KOPANO_TEST_USER} -S
  [ "$status" -eq 0 ]
  [[ "$output" =~ "Entry #0" ]]

  run kopano-ibrule -u ${KOPANO_TEST_USER} -D 1
}
