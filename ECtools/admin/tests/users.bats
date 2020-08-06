#!/usr/bin/env bats

@test "list all users" {
  run kopano-admin -l
  re="User list for Default\([[:digit:]]+\):"
  [ "$status" -eq 0 ]
  [[ "${lines[0]}" =~ "User list for Default" ]]
}

@test "list all groups" {
  run kopano-admin -L
  re="Group list for Default\([[:digit:]]+\):"
  [ "$status" -eq 0 ]
  [[ $lines[0]} =~ $re ]]
}

@test "list details of user" {
  run kopano-admin --details "$KOPANO_TEST_USER"
  [ "$status" -eq 0 ]
  [[ "$output" =~ "Auto-accept meeting req:no" ]]
  [[ "$output" =~ "Auto-process meeting req:no" ]]
}

@test "version" {
  run kopano-admin --version
  re="kopano-admin [[:digit:]]+.[[:digit:]]+.[[:digit:]]+"
  [ "$status" -eq 0 ]
  [[ "${lines[0]}" =~ $re ]]
}

@test "list orphans" {
  run kopano-admin --list-orphans
  [ "$status" -eq 0 ]
}

@test "list sendas" {
  run kopano-admin --list-sendas ${KOPANO_TEST_USER}
  re="Send-as list \([[:digit:]]\) for user ${KOPANO_TEST_USER}:"
  [ "$status" -eq 0 ]
  [[ $lines[0]} =~ $re ]]
}

@test "user count" {
  run kopano-admin --user-count
  re="Active		no limit	[[:digit:]]+	-"
  [ "$status" -eq 0 ]

}
