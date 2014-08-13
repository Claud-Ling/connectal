#!/usr/bin/python
## Copyright (c) 2013-2014 Quanta Research Cambridge, Inc.

## Permission is hereby granted, free of charge, to any person
## obtaining a copy of this software and associated documentation
## files (the "Software"), to deal in the Software without
## restriction, including without limitation the rights to use, copy,
## modify, merge, publish, distribute, sublicense, and/or sell copies
## of the Software, and to permit persons to whom the Software is
## furnished to do so, subject to the following conditions:

## The above copyright notice and this permission notice shall be
## included in all copies or substantial portions of the Software.

## THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
## EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
## MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
## NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
## BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
## ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
## CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
## SOFTWARE.

import os, sys, shutil, string
import argparse
import subprocess
import glob
import time
import syntax
import util
import boardinfo

supported_os = ['android', 'ubuntu']

argparser = argparse.ArgumentParser("Generate C++/BSV/Xilinx stubs for an interface.")
argparser.add_argument('bsvfile', help='BSV files to parse', nargs='+')
argparser.add_argument('-B', '--board', default='zc702', help='Target Board for compilation')
argparser.add_argument('-O', '--OS', default=None, choices=supported_os, help='Target operating system')
argparser.add_argument('-s2h', '--s2hinterface', help='BSV interface to generate stubs for software to hardware communication', action='append')
argparser.add_argument('-h2s', '--h2sinterface', help='BSV interface to generate stubs for hardware to software communication', action='append')
argparser.add_argument('-p', '--project-dir', default='./xpsproj', help='xps project directory')
argparser.add_argument('-s', '--source', help='C++ source files', action='append')
argparser.add_argument(      '--contentid', help='Specify 64-bit contentid for PCIe designs')
argparser.add_argument('-I', '--cinclude', help='Specify C++ include directories', default=[], action='append')
argparser.add_argument('-V', '--verilog', default=[], help='Additional verilog sources', action='append')
argparser.add_argument('--xci', default=[], help='Additional IP sources', action='append')
argparser.add_argument('-C', '--constraint', help='Additional constraint files', action='append')
argparser.add_argument('-M', '--make', help='Run make on the specified targets', action='append')
argparser.add_argument('-D', '--bsvdefine', default=[], help='BSV define', action='append')
argparser.add_argument('-l', '--clib', default=[], help='C++ libary', action='append')
argparser.add_argument('-S', '--clibfiles', default=[], help='C++ libary file', action='append')
argparser.add_argument('-L', '--clibdir', default=[], help='C++ libary', action='append')
argparser.add_argument('-T', '--tcl', default=[], help='Vivado tcl script', action='append')
argparser.add_argument('-m', '--bsimsource', help='Bsim C++ source files', action='append')
argparser.add_argument('-b', '--bscflags', default=[], help='Options to pass to the BSV compiler', action='append')
argparser.add_argument('--xelabflags', default=[], help='Options to pass to the xelab compiler', action='append')
argparser.add_argument('--xsimflags', default=[], help='Options to pass to the xsim simulator', action='append')
argparser.add_argument('--ipdir', help='Directory in which to store generated IP')
argparser.add_argument('-q', '--qtused', help='Qt used in Bsim test application', action='store_true')
argparser.add_argument('--stl', help='STL implementation to use for Android builds', default=None)
argparser.add_argument('--floorplan', help='Floorplan XDC', default=None)
argparser.add_argument('-P', '--partition-module', default=[], help='Modules to separately synthesize/place/route', action='append')
argparser.add_argument('--cachedir', default=None, help='Cache directory for fpgamake to use')
argparser.add_argument('-v', '--verbose', help='Display verbose information messages', action='store_true')

noisyFlag=False

tclReadVerilogTemplate='read_verilog [ glob %(verilog)s%(pattern)s ]'
tclReadXciTemplate='''
generate_target {Synthesis} [get_files %(xci)s]
read_ip %(xci)s
'''

tclfileConstraintTemplate='''read_xdc {./constraints/%(xdcname)s}'''

tclboardTemplate='''
set partname {%(partname)s}
set boardname {%(boardname)s}
set xbsvdir {%(xbsvdir)s}
set xbsvipdir {%(ipdir)s}
set needspcie {%(needspcie)s}
set xbsv_dut { %(Dut)s }
%(tcldefines)s
'''

tclzynqrewireclock = '''
foreach {pat} {CLK_GATE_hdmi_clock_if CLK_*deleteme_unused_clock* CLK_GATE_*deleteme_unused_clock* RST_N_*deleteme_unused_reset*} {
    foreach {net} [get_nets -quiet $pat] {
        puts "disconnecting net $net"
	disconnect_net -net $net -objects [get_pins -quiet -of_objects $net]
    }
}
'''

fpgamakeRuleTemplate='''
FPGAMAKE=$(XBSVDIR)/../fpgamake/fpgamake
fpgamake.mk: $(vfile) Makefile prepare_bin_target
	$(Q)mkdir -p hw
	$(Q)$(FPGAMAKE) $(FPGAMAKE_VERBOSE) -o fpgamake.mk %(partitions)s --floorplan=%(floorplan)s %(xdc)s %(xci)s %(sourceTcl)s -t $(MKTOP) %(cachedir)s -b hw/mkTop.bit verilog $(XBSVDIR)/verilog

hw/mkTop.bit: fpgamake.mk prepare_bin_target
	$(Q)make -f fpgamake.mk
	$(Q)cp -f Impl/*/*.rpt bin
'''

makefileTemplate='''

##    run: run the program
##         pass parameters to software via 'make RUN_ARGS= run'
RUN_ARGS=

export DTOP=%(project_dir)s
XBSVDIR=%(xbsvdir)s
BSVPATH = %(bsvpath)s

BOARD=%(boardname)s
MKTOP=%(topbsvmod)s
OS=%(OS)s
DUT=%(dut)s

export S2H = %(s2hinterface)s
export H2S = %(h2sinterface)s
BSVFILES = %(bsvfiles)s

BSCFLAGS_PROJECT = %(bscflags)s
BSIM_CXX_PROJECT = %(bsimsource)s
XELABFLAGS = %(xelabflags)s
XSIMFLAGS  = %(xsimflags)s
TOPBSVFILE = %(topbsvfile)s
BSVDEFINES = %(bsvdefines)s
QTUSED = %(qtused)s
export BSVDEFINES_LIST = %(bsvdefines_list)s
export DUT_NAME = %(Dut)s

%(mdefines)s

include $(XBSVDIR)/scripts/Makefile.xbsv.build

%(bitsmake)s
'''

androidmk_template='''
include $(CLEAR_VARS)
LOCAL_ARM_MODE := arm
include %(project_dir)s/jni/Makefile.generated_files
APP_SRC_FILES := $(addprefix %(project_dir)s/jni/,  $(GENERATED_CPP)) %(source)s
PORTAL_SRC_FILES := $(addprefix %(xbsvdir)s/cpp/, portal.c poller.cpp sock_utils.c timer.c)
LOCAL_SRC_FILES := $(APP_SRC_FILES) $(PORTAL_SRC_FILES)

LOCAL_PATH :=
LOCAL_MODULE := android_exe
LOCAL_MODULE_TAGS := optional
LOCAL_LDLIBS := -llog %(clibdirs)s %(clibs)s %(clibfiles)s
LOCAL_CPPFLAGS := "-march=armv7-a"
LOCAL_CFLAGS := -DZYNQ -I%(xbsvdir)s -I%(xbsvdir)s/cpp -I%(xbsvdir)s/lib/cpp -I%(xbsvdir)s/drivers/zynqportal -I%(project_dir)s/jni %(cincludes)s %(cdefines)s -I%(xbsvdir)s/drivers/portalmem
LOCAL_CXXFLAGS := -DZYNQ -I%(xbsvdir)s -I%(xbsvdir)s/cpp -I%(xbsvdir)s/lib/cpp -I%(xbsvdir)s/drivers/zynqportal -I%(project_dir)s/jni %(cincludes)s %(cdefines)s -I%(xbsvdir)s/drivers/portalmem

include $(BUILD_EXECUTABLE)
'''

linuxmakefile_template='''
export V=0
ifeq ($(V),0)
Q=@
else
Q=
endif

CFLAGS_COMMON = -O -g -I%(project_dir)s/jni -I%(xbsvdir)s -I%(xbsvdir)s/cpp -I%(xbsvdir)s/lib/cpp %(sourceincludes)s %(cincludes)s %(cdefines)s -I%(xbsvdir)s/drivers/portalmem -I%(xbsvdir)s/drivers/pcieportal -I%(xbsvdir)s/drivers/zynqportal
CFLAGS = $(CFLAGS_COMMON)

PORTAL_CPP_FILES = $(addprefix %(xbsvdir)s/cpp/, portal.c poller.cpp sock_utils.c timer.c)
include %(project_dir)s/jni/Makefile.generated_files
SOURCES = $(addprefix %(project_dir)s/jni/,  $(GENERATED_CPP)) %(source)s $(PORTAL_CPP_FILES)
LDLIBS := %(clibdirs)s %(clibs)s -pthread 

BSIM_EXE_CXX_FILES = BsimDma.cxx BsimCtrl.cxx TlpReplay.cxx
BSIM_EXE_CXX = $(addprefix %(xbsvdir)s/cpp/, $(BSIM_EXE_CXX_FILES))

ubuntu_exe: $(SOURCES)
	$(Q)g++ $(CFLAGS) -o ubuntu_exe $(SOURCES) $(LDLIBS)

bsim_exe: $(SOURCES)
	$(Q)g++ $(CFLAGS_COMMON) -o bsim_exe -DBSIM $(SOURCES) $(BSIM_EXE_CXX) $(LDLIBS)
'''

if __name__=='__main__':
    xbsvdir = os.path.dirname(os.path.abspath(sys.argv[0]))+'/../'
    options = argparser.parse_args()

    boardname = options.board.lower()
    option_info = boardinfo.attribute(boardname, 'options')
    # parse additional options together with sys.argv
    if option_info['XBSVFLAGS']:
        options=argparser.parse_args(option_info['XBSVFLAGS'] + sys.argv[1:])
    print options

    if options.verbose:
        noisyFlag = True
    if not options.source:
        options.source = []
    if not options.bsimsource:
        options.bsimsource = []
    if not options.constraint:
        options.constraint = []
    if not options.verilog:
        options.verilog = []
    if not options.tcl:
        options.tcl = []
    if not options.xsimflags:
        options.xsimflags = ['-R']

    if not options.s2hinterface:
        options.s2hinterface = []
    if not options.h2sinterface:
        options.h2sinterface = []

    project_dir = os.path.abspath(os.path.expanduser(options.project_dir))

    # remove intermediate files generated by parser generator
    # this is necessary due to silent failures when syntax.py is compiled
    os.path.exists('./out/parser.out')   and os.remove('./out/parser.out')
    os.path.exists('./out/parsetab.pyc') and os.remove('./out/parsetab.pyc')
    os.path.exists('./out/parsetab.py')  and os.remove('./out/parsetab.py')
    
    bsvdefines = options.bsvdefine
    bsvdefines.append('project_dir=%s' % project_dir)

    option_info['needs_pcie_7x_gen1x8'] = option_info['needs_pcie_7x_gen1x8'] == 'True'
    if option_info['rewireclockstring'] != '':
        option_info['rewireclockstring'] = tclzynqrewireclock

    dutname = 'mk' + option_info['TOP']
    topbsv = xbsvdir + '/bsv/' + option_info['TOP'] + '.bsv'
        
    rewireclockstring = option_info['rewireclockstring']
    needs_pcie_7x_gen1x8 = option_info['needs_pcie_7x_gen1x8']
    partname = option_info['partname']
    if not 'os' in options:
        options.os = option_info['os']
    bdef = option_info.get('bsvdefines')
    if bdef:
        bsvdefines += bdef
    cstr = option_info.get('constraints')
    if cstr:
        options.constraint.append(os.path.join(xbsvdir, cstr))

    bsvdefines += ['BOARD_'+boardname]

    options.verilog.append(os.path.join(xbsvdir, 'verilog'))
    options.constraint.append(os.path.join(xbsvdir, 'xilinx/constraints/%s.xdc' % boardname))

    tclboardname = os.path.join(project_dir, 'board.tcl')
    tclsynthname = os.path.join(project_dir, '%s-synth.tcl' % dutname.lower())
    makename = os.path.join(project_dir, 'Makefile')

    androidmkname = os.path.join(project_dir, 'jni', 'Android.mk')
    linuxmkname = os.path.join(project_dir, 'jni', 'Ubuntu.mk')
 
    if noisyFlag:
        print 'Writing Android.mk', androidmkname
    substs = {
        #android
	'project_dir': project_dir,
        #ubuntu
        'sourceincludes': ' '.join(['-I%s' % os.path.dirname(os.path.abspath(sf)) for sf in options.source]) if options.source else '',
        #common
        'source': ' '.join([os.path.abspath(sf) for sf in options.source]) if options.source else '',
        'xbsvdir': xbsvdir,
	'clibs': ' '.join(['-l%s' % l for l in options.clib]),
	'clibfiles': ' '.join(['%s' % l for l in options.clibfiles]),
	'clibdirs': ' '.join([ '-L%s' % os.path.abspath(l) for l in options.clibdir ]),
	'cdefines': ' '.join([ '-D%s' % d for d in bsvdefines ]),
	'cincludes': ' '.join([ '-I%s' % os.path.abspath(i) for i in options.cinclude ])
    }
    f = util.createDirAndOpen(androidmkname, 'w')
    f.write(androidmk_template % substs)
    f.close()
    f = util.createDirAndOpen(linuxmkname, 'w')
    f.write(linuxmakefile_template % substs)
    f.close()
    if options.stl:
	    f = util.createDirAndOpen(os.path.join(project_dir, 'jni', 'Application.mk'), 'w')
	    f.write('APP_STL                 := %s\n' % options.stl)
	    f.close()

    tclsubsts = {'dut': dutname.lower(),
                 'Dut': dutname,
                 'rewire_clock': rewireclockstring,
                 'project_dir': project_dir,
                 'partname': partname,
                 'boardname': boardname,
                 'xbsvdir': xbsvdir,
                 'tclfileConstraints': '\n'.join([tclfileConstraintTemplate
                                                  % { 'xdcname': os.path.basename(f) }
                                                  for f in options.constraint ]),
                 'read_verilog': '\n'.join([tclReadVerilogTemplate
                                            % { 'verilog': os.path.abspath(f),
						'pattern': '/*.v' if os.path.isdir(f) else ''} for f in options.verilog]),
                 'read_xci': '\n'.join([tclReadXciTemplate
                                        % { 'xci': f } for f in options.xci]),
                 'needspcie': 1 if needs_pcie_7x_gen1x8 else 0,
                 'tcldefines': '\n'.join(['set %s {%s}' % (var,val) for (var,val) in map(util.splitBinding, bsvdefines)]),
                 'ipdir': os.path.abspath(options.ipdir) if options.ipdir else xbsvdir
                 }
    tcl = util.createDirAndOpen(tclboardname, 'w')
    tcl.write(tclboardTemplate % tclsubsts)
    tcl.close()

    if options.constraint:
        for constraint in options.constraint:
            if noisyFlag:
                print 'Copying constraint file from', constraint
            dstconstraintdir = os.path.join(project_dir, 'constraints')
            if not os.path.exists(dstconstraintdir):
                os.makedirs(dstconstraintdir)
            ## this path is here so we can overwrite sources
            shutil.copy(constraint, dstconstraintdir)

    if noisyFlag:
        print 'Writing Makefile', makename
    make = util.createDirAndOpen(makename, 'w')

    bitsmake=fpgamakeRuleTemplate % {'partitions': ' '.join(['-s %s' % p for p in options.partition_module]),
					 'floorplan': os.path.abspath(options.floorplan) if options.floorplan else '',
					 'xdc': ' '.join(['--xdc=%s' % os.path.abspath(xdc) for xdc in options.constraint]),
					 'xci': ' '.join(['--xci=%s' % os.path.abspath(xci) for xci in options.xci]),
					 'sourceTcl': ' '.join(['--tcl=%s' % os.path.abspath(tcl) for tcl in options.tcl]),
					 'cachedir': '--cachedir=%s' % os.path.abspath(options.cachedir) if options.cachedir else ''
					 }

    make.write(makefileTemplate % {'xbsvdir': xbsvdir,
                                   'bsvpath': ':'.join(list(set([os.path.dirname(os.path.abspath(bsvfile)) for bsvfile in options.bsvfile]
                                                                + [os.path.join(xbsvdir, 'bsv')]
                                                                + [os.path.join(xbsvdir, 'lib/bsv')]
                                                                + [os.path.join(xbsvdir, 'generated/xilinx')]))),
                                   'bsvdefines': util.foldl((lambda e,a: e+' -D '+a), '', bsvdefines),
                                   'boardname': boardname,
                                   'OS': options.os,
                                   'qtused': 'cd jni; qmake ../..; make' if options.qtused else '',
                                   's2hinterface': ' '.join(options.s2hinterface),
                                   'h2sinterface': ' '.join(options.h2sinterface),
                                   'bsvfiles': ' '.join([ os.path.abspath(bsvfile) for bsvfile in options.bsvfile]),
                                   'sourcefiles': ' '.join([os.path.abspath(source) for source in options.source]) if options.source else '',
                                   'bsimsource': ' '.join([os.path.abspath(bsimsource) for bsimsource in options.bsimsource]) if options.bsimsource else '',
                                   'includepath': ' '.join(['-I%s' % os.path.dirname(os.path.abspath(source)) for source in options.source]) if options.source else '',
                                   'project_dir': project_dir,
                                   'topbsvfile' : os.path.abspath(topbsv),
                                   'topbsvmod'  : dutname,
                                   'dut' : dutname.lower(),
                                   'Dut': dutname,
                                   'clibs': ' '.join(['-l%s' % l for l in options.clib]),
                                   'cdefines': ' '.join([ '-D%s' % d for d in bsvdefines ]),
                                   'mdefines': '\n'.join(['%s="%s"' % (var,val) for (var,val) in map(util.splitBinding, bsvdefines)]),
                                   'bscflags': ' '.join(options.bscflags),
                                   'xelabflags': ' '.join(options.xelabflags),
                                   'xsimflags': ' '.join(options.xsimflags),
                                   'bsvdefines_list': ' '.join(bsvdefines),
                                   'bitsmake': bitsmake
                                   })
    make.close()

    if options.make:
        os.chdir(project_dir)
        os.putenv('PWD', subprocess.check_output(['pwd'])[0:-1])
        subprocess.call(['make'] + options.make)
