#!/usr/bin/python

import json
import hyperclient

data = []
with open('hotels.json') as f:
    for line in f:
        try:
            res = json.loads(line)
        except ValueError:
            continue
        else:
            data.append(res)

c = hyperclient.Client('172.31.0.5', 1982)

for d in data:
    key  = d['key'].encode('ascii', 'ignore')
    name = d['name'].encode('ascii', 'ignore')
    loc  = d['location'].encode('ascii', 'ignore')
    price = d['price']
    stars = d['stars']
    ratings = d['ratings']
    reviews = d['reviews']

    c.put('hotels', key, {'name':name, 'location':loc, 'prices':float(price), 'stars':int(stars), 'ratings':float(ratings), 'reviews':int(reviews)})


print "DONE LOADING"
res1 = c.get('hotels', 'e0kHb')
print res1

res2 = c.get('hotels', 'o50pu0')
print res2
