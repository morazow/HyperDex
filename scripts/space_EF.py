#!/usr/bin/python

import hyperclient
c = hyperclient.Client('node29', 1982)
c.add_space('''
    space usertable
    dimensions key, recno, field0, field1, field2, field3, field4, field5, field6, field7, field8, field9
    key key auto 4 2
    subspace recno range:range, field0 none:none, field1 none:none, field2 none:none, field3 none:none, field4 none:none, field5 none:none, field6 none:none, field7 none:none, field8 none:none, field9 none:none auto 4 2
    subspace field0, field1, field2, field3, field4 auto 4 2
    subspace field5, field6, field7, field8, field9 auto 4 2
        ''')

