#!/usr/bin/env python
# encoding: utf-8

"""
  > FileName: string_time_consuming.py
  > Author: Yin
  > Mail: yjd48676@ly.com
  > CreatedTime: 2018/7/30 19:20
"""

import time


def print_time(func):

    def wrapped():
        start = time.clock()
        func()
        end = time.clock()
        print "Running time: {}".format(end-start)
        return

    return wrapped


STR_LIST = []
for i in range(100000):
    # 避免单字符串
    STR_LIST.append(str(i) + '00-11-22-33-44-55-66')


@print_time
def test_concat():
    strs = ''
    for item in STR_LIST:
        strs += item


@print_time
def test_join():
    strs = ''
    strs.join(STR_LIST)


if __name__ == '__main__':

    test_concat()
    test_join()

