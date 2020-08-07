#!/usr/bin/env bats

@test "public folders" {
  run kopano-fsck --autofix=yes -u SYSTEM --public --checkonly --acceptdisclaimer
  [ "$status" -eq 0 ]
}

@test "" {
  run kopano-fsck -u ${KOPANO_TEST_USER} --checkonly --acceptdisclaimer
  [ "$status" -eq 0 ]
}
