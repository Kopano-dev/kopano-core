#!/usr/bin/env bats

teardown() {
  kopano-mailbox-permissions --remove-all-permissions ${KOPANO_TEST_USER}
}

@test "mailbox-permissions --list-permissions" {
  run kopano-mailbox-permissions --list-permissions ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]
  [[ "$output" =~ "${KOPANO_TEST_USER}" ]]
}

@test "mailbox-permissions --list-permissions-per-folder" {
  run kopano-mailbox-permissions --list-permissions-per-folder ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]
  [[ "$output" =~ "${KOPANO_TEST_USER}" ]]
}

@test "mailbox-permissions delegate permission" {
  run kopano-mailbox-permissions --update-delegate ${KOPANO_TEST_USER3} --calendar secretary ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]
  [[ "$output" =~ "${KOPANO_TEST_USER}" ]]

  run kopano-mailbox-permissions --list-permissions ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]
  regex="Calendar\s*| ${KOPANO_TEST_FULLNAME3}:secretary"
  [[ "$output" =~ $regex ]]
  regex="Freebusy Data\s*| ${KOPANO_TEST_FULLNAME3}:secretary"
  [[ "$output" =~ $regex ]]
}

@test "mailbox-permissions see private" {
  run kopano-mailbox-permissions --update-delegate ${KOPANO_TEST_USER3} --calendar secretary --seeprivate yes ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]
  [[ "$output" =~ "${KOPANO_TEST_USER}" ]]

  run kopano-mailbox-permissions --list-permissions ${KOPANO_TEST_USER}
  [ "$status" -eq 0 ]
  regex="${KOPANO_TEST_FULLNAME3} | True\s*| False"
  [[ "$output" =~ $regex ]]
}

@test "mailbox-permissions list all permissions" {
  run kopano-mailbox-permissions --list-permissions-per-folder -a
  [ "$status" -eq 0 ]
}
