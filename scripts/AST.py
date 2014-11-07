# Copyright (c) 2014 Quanta Research Cambridge, Inc
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

import math
import globalv

import os
import sys
import traceback
import AST
import util
import functools
import json
import bsvgen

def dtInfo(arg):
    rc = {}
    if hasattr(arg, 'name'):
        rc['name'] = arg.name
    if hasattr(arg, 'type'):
        rc['type'] = arg.type
    if hasattr(arg, 'cName'):
        rc['cName'] = arg.cName()
    if hasattr(arg, 'bitWidth'):
        rc['bitWidth'] = arg.bitWidth()
    if hasattr(arg, 'params'):
        #print 'OOO', arg.params
        if arg.params is not None:
            rc['params'] = [dtInfo(p) for p in arg.params]
    if hasattr(arg, 'elements'):
        if arg.type == 'Enum':
            rc['elements'] = arg.elements
        else:
            rc['elements'] = [piInfo(p.name, p.type) for p in arg.elements]
    return rc

def indent(f, indentation):
    for i in xrange(indentation):
        f.write(' ')

class NoCMixin:
    def emitCDeclaration(self, f, indentation):
        pass

class MethodMixin:
    def collectTypes(self):
        result = [self.return_type]
        result.append(AST.Type('Tuple', self.params))
        return result

class StructMemberMixin:
    def emitCDeclaration(self, f, indentation):
        indent(f, indentation)
        f.write('%s %s' % (self.type.cName(), self.name))
        if self.type.isBitField():
            f.write(' : %d' % self.type.bitWidth())
        f.write(';\n')

class TypeDefMixin:
    def emitCDeclaration(self,f,indentation):
        #print 'TypeDefMixin.emitCdeclaration', self.tdtype.type, self.name, self.tdtype
        if self.tdtype.type == 'Struct' or self.tdtype.type == 'Enum':
            self.tdtype.emitCDeclaration(self.name,f,indentation)
        elif self.name == 'SpecialTypeForSendingFd':
            pass
        elif False and self.tdtype.type == 'Type':
            tdtype = self.tdtype
            #print 'resolving', tdtype.type, tdtype
            while tdtype.type == 'Type' and globalv.globalvars.has_key(tdtype.name):
                td = globalv.globalvars[tdtype.name]
                tdtype = td.tdtype.instantiate(dict(zip(td.params, tdtype.params)))
                #print 'resolved to', tdtype.type, tdtype
            if tdtype.type != 'Type':
                #print 'emitting declaration'
                tdtype.emitCDeclaration(self.name,f,indentation)

class StructMixin:
    def collectTypes(self):
        result = [self]
        result.append(self.elements)
        return result
    def emitCDeclaration(self, name, f, indentation):
        indent(f, indentation)
        if (indentation == 0):
            f.write('typedef ')
        f.write('struct %s {\n' % name)
        for e in self.elements:
            e.emitCDeclaration(f, indentation+4)
        indent(f, indentation)
        f.write('}')
        if (indentation == 0):
            f.write(' %s;' % name)
        f.write('\n')

class EnumElementMixin:
    def cName(self):
        return self.name

class EnumMixin:
    def cName(self):
        return self.name
    def collectTypes(self):
        return [self]
    def emitCDeclaration(self, name, f, indentation):
        indent(f, indentation)
        if (indentation == 0):
            f.write('typedef ')
        f.write('enum %s { ' % name)
        indent(f, indentation)
        f.write(', '.join(['%s_%s' % (name, e) for e in self.elements]))
        indent(f, indentation)
        f.write(' }')
        if (indentation == 0):
            f.write(' %s;' % name)
        f.write('\n')
    def bitWidth(self):
        return int(math.ceil(math.log(len(self.elements))))

class InterfaceMixin:
    def collectTypes(self):
        return [d.collectTypes() for d in self.decls]
    def getSubinterface(self, name):
        subinterfaceName = name
        if not globalv.globalvars.has_key(subinterfaceName):
            return None
        subinterface = globalv.globalvars[subinterfaceName]
        #print 'subinterface', subinterface, subinterface
        return subinterface
    def assignRequestResponseChannels(self, channelNumber=0):
        for d in self.decls:
            if d.__class__ == AST.Method:
                d.channelNumber = channelNumber
                channelNumber = channelNumber + 1
        self.channelCount = channelNumber
    def parentClass(self, default):
        rv = default if (len(self.typeClassInstances)==0) else (self.typeClassInstances[0])
        return rv
    def global_name(self, s, suffix):
        return '%s%s_%s' % (cName(self.name), suffix, s)

class ParamMixin:
    def cName(self):
        return self.name
    def emitCDeclaration(self, f, indentation):
        indent(f, indentation)
        f.write('s %s' % (self.type, self.name))

class TypeMixin:
    def cName(self):
        cid = self.name.replace(' ', '')
        if cid == 'Bit':
            if self.params[0].numeric() <= 32:
                return 'uint32_t'
            elif self.params[0].numeric() <= 64:
                return 'uint64_t'
            else:
                return 'std::bitset<%d>' % (self.params[0].numeric())
        elif cid == 'Int':
            if self.params[0].numeric() == 32:
                return 'int'
            else:
                assert(False)
        elif cid == 'UInt':
            if self.params[0].numeric() == 32:
                return 'unsigned int'
            else:
                assert(False)
        elif cid == 'Float':
            return 'float'
        elif cid == 'Vector':
            return 'bsvvector<%d,%s>' % (self.params[0].numeric(), self.params[1].cName())
        elif cid == 'Action':
            return 'int'
        elif cid == 'ActionValue':
            return self.params[0].cName()
        if self.params:
            name = '%sL_%s_P' % (cid, '_'.join([cName(t) for t in self.params if t]))
        else:
            name = cid
        return name
    def isBitField(self):
        return self.name == 'Bit' or self.name == 'Int' or self.name == 'UInt'
    def bitWidth(self):
        if self.name == 'Bit' or self.name == 'Int' or self.name == 'UInt':
            width = self.params[0].name
            while globalv.globalvars.has_key(width):
                decl = globalv.globalvars[width]
                if decl.type != 'TypeDef':
                    break
                print 'Resolving width', width, decl.tdtype
                width = decl.tdtype.name
            return int(width)
        if self.name == 'Float':
            return 32
        else:
            return 0

def cName(x):
    if type(x) == str or type(x) == unicode:
        x = x.replace(' ', '')
        x = x.replace('.', '$')
        return x
    else:
        return x.cName()

def piInfo(name, type):
    rc = {}
    rc['name'] = name
    rc['type'] = dtInfo(type)
    return rc

def declInfo(name, params):
    rc = {}
    rc['name'] = name
    rc['params'] = []
    for pitem in params:
        rc['params'].append(piInfo(pitem.name, pitem.type))
    return rc

def classInfo(item):
    rc = {}
    rc['name'] = item.name
    rc['parentLportal'] = item.parentClass("portal")
    rc['parentPortal'] = item.parentClass("Portal")
    rc['package'] = item.package
    rc['BSV'] = bsvgen.substsTemplate(item)
    rc['decls'] = []
    for mitem in item.decls:
        rc['decls'].append(declInfo(mitem.name, mitem.params))
    return rc

def serialize_json(interfaces, globalimports, dutname):
    itemlist = []
    for item in interfaces:
        itemlist.append(classInfo(item))
    jfile = open('cppgen_intermediate_data.tmp', 'w')
    toplevel = {}
    toplevel['interfaces'] = itemlist
    gvlist = {}
    for key, value in globalv.globalvars.iteritems():
        gvlist[key] = {'type': value.type}
        if value.type == 'TypeDef':
            #print 'TYPEDEF globalvar:', key, value
            gvlist[key]['name'] = value.name
            gvlist[key]['tdtype'] = dtInfo(value.tdtype)
            gvlist[key]['params'] = value.params
        else:
            print 'Unprocessed globalvar:', key, value
    toplevel['globalvars'] = gvlist
    gdlist = []
    for item in globalv.globaldecls:
        newitem = {'type': item.type}
        if item.type == 'TypeDef':
            newitem['name'] = item.name
            newitem['tdtype'] = dtInfo(item.tdtype)
            newitem['params'] = item.params
            #print 'TYPEDEF globaldecl:', item, 'ZZZ', newitem
        else:
            print 'Unprocessed globaldecl:', item, 'ZZZ', newitem
        gdlist.append(newitem)
    toplevel['globaldecls'] = gdlist
    toplevel['globalimports'] = globalimports
    toplevel['dutname'] = dutname
    json.dump(toplevel, jfile, sort_keys = True, indent = 4)
    jfile.close()
    j2file = open('cppgen_intermediate_data.tmp').read()
    jsondata = json.loads(j2file)
    return jsondata

class Method(MethodMixin):
    def __init__(self, name, return_type, params):
        self.type = 'Method'
        self.name = name
        self.return_type = return_type
        self.params = params
    def __repr__(self):
        sparams = [p.__repr__() for p in self.params]
        return '<method: %s %s %s>' % (self.name, self.return_type, sparams)
    def instantiate(self, paramBindings):
        #print 'instantiate method', self.name, self.params
        return Method(self.name,
                      self.return_type.instantiate(paramBindings),
                      [ p.instantiate(paramBindings) for p in self.params])

class Function(NoCMixin):
    def __init__(self, name, return_type, params):
        self.type = 'Function'
        self.name = name
        self.return_type = return_type
        self.params = params
    def __repr__(self):
        if not self.params:
            return '<function: %s %s NONE>' % (self.name, self.return_type)
        sparams = map(str, self.params)
        return '<function: %s %s %s>' % (self.name, self.return_type, sparams)

class Variable:
    def __init__(self, name, t):
        self.type = 'Variable'
        self.name = name
        self.type = t
    def __repr__(self):
        return '<variable: %s : %s>' % (self.name, self.type)

class Interface(InterfaceMixin):
    def __init__(self, name, params, decls, subinterfacename, packagename):
        self.type = 'Interface'
        self.name = name
        self.params = params
        self.decls = decls
        self.subinterfacename = subinterfacename
        self.typeClassInstances = []
        self.package = packagename
    def interfaceType(self):
        return Type(self.name,self.params)
    def __repr__(self):
        return '{interface: %s (%s) : %s}' % (self.name, self.params, self.typeClassInstances)
    def instantiate(self, paramBindings):
        newInterface = Interface(self.name, [],
                                 [d.instantiate(paramBindings) for d in self.decls],
                                 self.subinterfacename,
                                 self.package)
        newInterface.typeClassInstances = self.typeClassInstances
        return newInterface

class Typeclass:
    def __init__(self, name):
        self.name = name
        self.type = 'TypeClass'
    def __repr__(self):
        return '{typeclass %s}' % (self.name)

class TypeclassInstance:
    def __init__(self, name, params, provisos, decl):
        self.name = name
        self.params = params
        self.provisos = provisos
        self.decl = decl
        self.type = 'TypeclassInstance'
    def __repr__(self):
        return '{typeclassinstance %s %s}' % (self.name, self.params)

class Module(NoCMixin):
    def __init__(self, moduleContext, name, params, interface, provisos, decls):
        self.type = 'Module'
        self.name = name
        self.moduleContext = moduleContext
        self.interface = interface
        self.params = params
        self.provisos = provisos
        self.decls = decls
    def __repr__(self):
        return '{module: %s %s}' % (self.name, self.decls)
    def collectTypes(self):
        result = []
        for d in self.decls:
            if d:
                result.extend(d.collectTypes())
        return result

class EnumElement(EnumElementMixin):
    def __init__(self, name, qualifiers, value):
        self.qualifiers = qualifiers
        self.value = value
    def __repr__(self):
        return '{enumelt: %s}' % (self.name)

class Enum(EnumMixin):
    def __init__(self, elements):
        self.type = 'Enum'
        self.elements = elements
    def __repr__(self):
        return '{enum: %s}' % (self.elements)
    def instantiate(self, paramBindings):
        return self

class StructMember(StructMemberMixin):
    def __init__(self, t, name):
        self.type = t
        self.name = name
    def __repr__(self):
        return '{field: %s %s}' % (self.type, self.name)
    def instantiate(self, paramBindings):
        return StructMember(self.type.instantiate(paramBindings), self.name)

class Struct(StructMixin):
    def __init__(self, elements):
        self.type = 'Struct'
        self.elements = elements
    def __repr__(self):
        return '{struct: %s}' % (self.elements)
    def instantiate(self, paramBindings):
        return Struct([e.instantiate(paramBindings) for e in self.elements])

class TypeDef(TypeDefMixin):
    def __init__(self, tdtype, name, params):
        self.name = name
        self.params = params
        self.type = 'TypeDef'
        self.tdtype = tdtype
        if tdtype.type != 'Type':
            tdtype.name = name
        self.type = 'TypeDef'
    def __repr__(self):
        return '{typedef: %s %s}' % (self.tdtype, self.name)

class Param(ParamMixin):
    def __init__(self, name, t):
        self.name = name
        self.type = t
    def __repr__(self):
        return '{param %s: %s}' % (self.name, self.type)
    def instantiate(self, paramBindings):
        return Param(self.name,
                     self.type.instantiate(paramBindings))

class Type(TypeMixin):
    def __init__(self, name, params):
        self.type = 'Type'
        self.name = name
        if params:
            self.params = params
        else:
            self.params = []
    def __repr__(self):
        sparams = map(str, self.params)
        return '{type: %s %s}' % (self.name, sparams)
    def instantiate(self, paramBindings):
        #print 'Type.instantiate', self.name, paramBindings
        if paramBindings.has_key(self.name):
            return paramBindings[self.name]
        else:
            return Type(self.name, [p.instantiate(paramBindings) for p in self.params])
    def numeric(self):
        if globalv.globalvars.has_key(self.name):
            decl = globalv.globalvars[self.name]
            if decl.type == 'TypeDef':
                return decl.tdtype.numeric()
        elif self.name in ['TAdd', 'TSub', 'TMul', 'TDiv', 'TLog', 'TExp', 'TMax', 'TMin']:
            values = [p.numeric() for p in self.params]
            if self.name == 'TAdd':
                return values[0] + values[1]
            elif self.name == 'TSub':
                return values[0] - values[1]
            elif self.name == 'TMul':
                return values[0] * values[1]
            elif self.name == 'TDiv':
                return math.ceil(values[0] / float(values[1]))
            elif self.name == 'TLog':
                return math.ceil(math.log(values[0], 2))
            elif self.name == 'TExp':
                return math.pow(2, values[0])
            elif self.name == 'TMax':
                return max(values[0], values[1])
            elif self.name == 'TMax':
                return min(values[0], values[1])
        return int(self.name)
