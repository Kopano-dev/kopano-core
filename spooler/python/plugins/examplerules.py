import MAPI
from MAPI.Util import *
from MAPI.Time import *
from MAPI.Struct import *

from plugintemplates import *

class examplerules1(IMapiDAgentPlugin):

    def PreRuleProcess(self, session, addrbook, store, rulestable):
        props = store.GetProps([PR_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID], 0)
        storeid = props[0].Value
        folderid = props[1].Value

        rowlist = [ ROWENTRY(
                        ROW_ADD,
                            [   SPropValue(PR_RULE_LEVEL, 0),
                                SPropValue(PR_RULE_NAME, "dagenttest"),
                                SPropValue(PR_RULE_PROVIDER, "RuleOrganizer"),
                                SPropValue(PR_RULE_STATE, ST_ENABLED),
                                SPropValue(PR_RULE_SEQUENCE, 1),
                                SPropValue(PR_RULE_ACTIONS, ACTIONS(EDK_RULES_VERSION, [ACTION(ACTTYPE.OP_MOVE, 0x00000000, None, None, 0x00000000, actMoveCopy(storeid, folderid) ) ] ) ),
                                SPropValue(PR_RULE_CONDITION, SContentRestriction(FL_SUBSTRING|FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, 'rulestest') ) )
                            ]
                        )
                ]
        rulestable.ModifyTable(0, rowlist)

        return MP_CONTINUE
