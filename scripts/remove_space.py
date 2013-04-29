#!/usr/bin/python
import hyperclient

c = hyperclient.Client('172.31.0.5', 1982)
c.rm_space('hotels')
