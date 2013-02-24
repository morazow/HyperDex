#!/usr/bin/python

import hyperclient
c = hyperclient.Client('thera', 1982)
c.add_space('''
        space usertable
        key k
        attributes field0, field1, field2, field3, field4,
        field5, field6, field7, field8, field9
        create 24 partitions
        ''')

