#!/usr/bin/env bats

@test "session stats" {
  run kopano-stats --sessions
  [ "$status" -eq 0 ]
}

@test "servers stats" {
  run kopano-stats --servers
  [ "$status" -eq 0 ]
  # no multi-server
  [  -z $output ]
}

@test "users stats" {
  run kopano-stats --users
  [ "$status" -eq 0 ]
}

@test "company stats" {
  run kopano-stats --company
  [ "$status" -eq 0 ]

  # only available on multiserver
  [[ "$output" =~ "not found" ]]
}
@test "system stats" {
  run grep userplugin <( kopano-stats --system)
  [ "$status" -eq 0 ]
  [[ "$output" =~ "ldap" ]]
}
