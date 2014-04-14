#!/usr/bin/python
# Copyright (c) 2013 Quanta Research Cambridge, Inc.
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use, copy,
# modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from __future__ import print_function
import sys
import json

boardfile = sys.argv[1]
pinoutfile = sys.argv[2]

pinstr = open(pinoutfile).read()
pinout = json.loads(pinstr)

boardInfo = json.loads(open(boardfile).read())

template='''\
set_property LOC "%(LOC)s" [get_ports "%(name)s"]
set_property IOSTANDARD "%(IOSTANDARD)s" [get_ports "%(name)s"]
set_property PIO_DIRECTION "%(PIO_DIRECTION)s" [get_ports "%(name)s"]
'''
setPropertyTemplate='''\
set_property %(prop)s "%(val)s" [get_ports "%(name)s"]
'''

for pin in pinout:
    pinInfo = pinout[pin]
    loc = 'TBD'
    iostandard = 'TBD'
    if pinInfo.has_key('fmc'):
        fmcPin = pinInfo['fmc']
        if boardInfo.has_key('fmc2'):
            fmcInfo = boardInfo['fmc2']
        else:
            fmcInfo = boardInfo['fmc']
        if fmcInfo.has_key(fmcPin):
            loc = fmcInfo[fmcPin]['LOC']
            iostandard = fmcInfo[fmcPin]['IOSTANDARD']
        else:
            print('Missing pin description for', fmcPin, file=sys.stderr)
            loc = 'fmc.%s' % (fmcPin)
        if pinInfo.has_key('IOSTANDARD'):
            iostandard = pinInfo['IOSTANDARD']
    print(template % {
        'name': pin,
        'LOC': loc,
        'IOSTANDARD': iostandard,
        'PIO_DIRECTION': pinInfo['PIO_DIRECTION']
        })
    for k in pinInfo:
        if k in ['fmc', 'IOSTANDARD', 'PIO_DIRECTION']: continue
        print (setPropertyTemplate % {
                'name': pin,
                'prop': k,
                'val': pinInfo[k],
                })

