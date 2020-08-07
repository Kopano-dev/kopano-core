#!/usr/bin/env bats

teardown() {
  kopano-oof -u ${KOPANO_TEST_USER} -m 0
}

@test "check oof status" {
  run kopano-oof -u ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]

  set=$(echo $output | jq .set)
  [ "$set" = "false" ]
}

@test "enable oof" {
  run kopano-oof -u ${KOPANO_TEST_USER} -m 1
  [ "$status" -eq 0 ]

  active=$(echo $output | jq .active)
  [ "$active" = "true" ]

  set=$(echo $output | jq .set)
  [ "$set" = "true" ]
}
