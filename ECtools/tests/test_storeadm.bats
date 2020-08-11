#!/usr/bin/env bats

teardown() {
  # Recreate user4's store
  kopano-storeadm -Cn ${KOPANO_TEST_USER4} || true
}


@test "store already exists for ${KOPANO_TEST_USER}" {
  run kopano-storeadm -Cn ${KOPANO_TEST_USER}
  [ "$status" -eq 1 ]
}

@test "copy store to public" {
  # unhook store for user4
  run kopano-storeadm -Dn ${KOPANO_TEST_USER4}
  [ "$status" -eq 0 ]

  GUID=${lines[1]##* }

  run kopano-storeadm -A ${GUID} -p
  [ "$status" -eq 0 ]

  # remove store
  run kopano-storeadm -R ${GUID}
  [ "$status" -eq 0 ]
}

@test "orphan store" {
  run kopano-storeadm -O ${KOPANO_TEST_USER4}
  [ "$status" -eq 0 ]
  [[ ! "$output" =~ "${KOPANO_TEST_USER4}" ]]

  # unhook store for user4
  run kopano-storeadm -Dn ${KOPANO_TEST_USER4}
  [ "$status" -eq 0 ]

  GUID=${lines[1]##* }

  run kopano-storeadm -O ${KOPANO_TEST_USER4}
  [ "$status" -eq 0 ]
  [[ "$output" =~ "${KOPANO_TEST_USER4}" ]]

  # remove store
  run kopano-storeadm -R ${GUID}
  [ "$status" -eq 0 ]
}

@test "unhook and rehook" {
  # unhook store for user4
  run kopano-storeadm -Dn ${KOPANO_TEST_USER4}
  [ "$status" -eq 0 ]

  GUID=${lines[1]##* }

  run kopano-storeadm -A ${GUID} -n ${KOPANO_TEST_USER4}
  [ "$status" -eq 0 ]
}

@test "version" {
  run kopano-storeadm -V
  re="kopano-storeadm [[:digit:]]+.[[:digit:]]+.[[:digit:]]+"
  [ "$status" -eq 0 ]
  [[ "${lines[0]}" =~ $re ]]
}
