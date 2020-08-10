#!/usr/bin/env bats

teardown() {
  # Reset default values
  kopano-admin -u ${KOPANO_TEST_USER} --mr-accept no
  kopano-admin -u ${KOPANO_TEST_USER} --mr-process no
  kopano-admin -u ${KOPANO_TEST_USER} --mr-decline-conflict no
  kopano-admin -u ${KOPANO_TEST_USER} --mr-decline-recurring no
}

@test "enable autoaccept" {
  run kopano-admin -u ${KOPANO_TEST_USER} --mr-accept yes
  [ "$status" -eq 0 ]
  [ ${output} = "User information updated." ]

  run kopano-admin --details "$KOPANO_TEST_USER"
  [ "$status" -eq 0 ]
  [[ "$output" =~ "Auto-accept meeting req:yes" ]]
  [[ "$output" =~ "Decline dbl meetingreq:	no" ]]
  [[ "$output" =~ "Decline recur meet.req:	no" ]]

  run kopano-admin -u ${KOPANO_TEST_USER} --mr-decline-conflict yes
  [ "$status" -eq 0 ]

  run kopano-admin -u ${KOPANO_TEST_USER} --mr-decline-recurring yes
  [ "$status" -eq 0 ]

  run kopano-admin --details "$KOPANO_TEST_USER"
  [ "$status" -eq 0 ]
  [[ "$output" =~ "Auto-accept meeting req:yes" ]]
  [[ "$output" =~ "Decline dbl meetingreq:	yes" ]]
  [[ "$output" =~ "Decline recur meet.req:	yes" ]]
}

@test "enable autoprocess" {
  run kopano-admin -u ${KOPANO_TEST_USER} --mr-process yes
  [ "$status" -eq 0 ]
  [ ${output} = "User information updated." ]

  run kopano-admin --details "$KOPANO_TEST_USER"
  [ "$status" -eq 0 ]

  # Setting mr-process is currently broken
  [[ "$output" =~ "Auto-process meeting req:yes" ]]
}
