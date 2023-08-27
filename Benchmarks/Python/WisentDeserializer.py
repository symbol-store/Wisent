#!/usr/bin/env python3

import sys
datasetSuffix = sys.argv[1] if len(sys.argv) > 1 else ""

from multiprocessing import shared_memory
from multiprocessing import Process, resource_tracker
def remove_shm_from_resource_tracker():
    """Monkey-patch multiprocessing.resource_tracker so SharedMemory won't be tracked
    More details at: https://bugs.python.org/issue38119
    """
    def fix_register(name, rtype):
        if rtype == "shared_memory":
            return
        return resource_tracker._resource_tracker.register(self, name, rtype)
    resource_tracker.register = fix_register

    def fix_unregister(name, rtype):
        if rtype == "shared_memory":
            return
        return resource_tracker._resource_tracker.unregister(self, name, rtype)
    resource_tracker.unregister = fix_unregister

    if "shared_memory" in resource_tracker._CLEANUP_FUNCS:
        del resource_tracker._CLEANUP_FUNCS["shared_memory"]

import requests

import struct
import ctypes

from enum import Enum
 
class ArgType(Enum):
    BOOL = 0
    LONG = 1
    DOUBLE = 2
    STRING = 3
    SYMBOL = 4
    EXPRESSION = 5
    
class Expression:
    def __init__(self, head):
        self.head = head
        self.args = []
    def __str__(self):
        return "('" + self.head + " ".join([str(c) for c in self.args]) + ")"
    def __repr__(self):
        return self.__str__()
    
class Symbol:
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return "'" + self.name
    def __repr__(self):
        return self.__str__()

def argsToDictionary(expr):
    dict = {}
    for arg in expr.args:
        dict[arg.head] = arg.args[0]
    return dict

def argsToTable(table):
    dict = {}
    for column in table.args:
        dict[column.args[0].name] = column.args[1]
    return dict
    
def readExpression(offset, args, argTypes, exprs, strings):
    head, startChild, endChild = struct.unpack("@QQQ", exprs[offset*24:(offset+1)*24])
    headStr = readSymbol(head, strings).name
    expr = Expression(headStr)
    readArgs(expr.args, startChild, endChild, args, argTypes, exprs, strings)
    if(headStr == "Object"):
        return argsToDictionary(expr)
    elif(headStr == "List"):
        return expr.args
    elif(headStr == "Table"):
        return argsToTable(expr)
    else:
        return expr

def readArgs(outputArgs, startChild, endChild, args, argTypes, exprs, strings):
    while(startChild < endChild):
        argType = struct.unpack("@Q", argTypes[startChild*8:(startChild+1)*8])[0]
        if(argType & 0x80):
            argType &= ~0x80
            argCount = struct.unpack("@I", argTypes[(startChild+1)*8:(startChild+2)*8-4])[0]
            #print("unpacking RLE - argType:" + str(argType) + " argCount:" + str(argCount))
            for i in range(startChild, startChild + argCount):
                outputArgs.append(readArgWithType(argType, i, args, argTypes, exprs, strings))
            startChild += argCount
        else:
            outputArgs.append(readArgWithType(argType, startChild, args, argTypes, exprs, strings))
            startChild += 1
    
def readString(offset, strings):
    leftChars = len(strings[offset:])
    partialSize = 32 if leftChars > 32 else leftChars
    partialStr = struct.unpack("@" + str(partialSize) + "s", strings[offset:offset+partialSize])[0]
    concatStr = ctypes.create_string_buffer(partialStr).value.decode("utf-8")
    if(len(concatStr) == partialSize):
        concatStr += readString(offset + partialSize, strings)
    concatStr = concatStr
    return concatStr

def readSymbol(offset, strings):
    str = readString(offset, strings)
    return Symbol(str)
    
def readArg(offset, args, argTypes, exprs, strings):
    argType = struct.unpack("@Q", argTypes[offset*8:(offset+1)*8])[0]
    return readArgWithType(argType, offset, args, argTypes, exprs, strings)

def readArgWithType(argType, offset, args, argTypes, exprs, strings):
    match ArgType(argType):
        case ArgType.BOOL:
            return struct.unpack("@?", args[(offset+1)*8-1:(offset+1)*8])[0]
        case ArgType.LONG:
            return struct.unpack("@Q", args[offset*8:(offset+1)*8])[0]
        case ArgType.DOUBLE:
            return struct.unpack("@d", args[offset*8:(offset+1)*8])[0]
        case ArgType.STRING:
            index = struct.unpack("@Q", args[offset*8:(offset+1)*8])[0]
            return readString(index, strings)
        case ArgType.SYMBOL:
            index = struct.unpack("@Q", args[offset*8:(offset+1)*8])[0]
            return readSymbol(index, strings)
        case ArgType.EXPRESSION:
            index = struct.unpack("@Q", args[offset*8:(offset+1)*8])[0]
            return readExpression(index, args, argTypes, exprs, strings)

def deserialize(buffer):
    argCount, exprCount = struct.unpack("@QQ", buffer[:16])
    offset = 32 # skip originalAddress, stringArgumentsFillIndex
    argsBufferSize = argCount*8
    args = buffer[offset:offset+argsBufferSize]
    offset += argsBufferSize
    argTypesBufferSize = argCount*8
    argTypes = buffer[offset:offset+argTypesBufferSize]
    offset += argTypesBufferSize
    exprsBufferSize = exprCount*24
    exprs = buffer[offset:offset+exprsBufferSize]
    offset += exprsBufferSize
    strings = buffer[offset:]
    return readArg(0, args, argTypes, exprs, strings)

def main():
    # request server to load data
    URL="http://localhost:3000"
    datapackageName = "datapackage" + datasetSuffix + ".json"
    resp = requests.get(url=URL+'/load', params={'name':'datapackage', 'path':'../Data/owid-deaths/' + datapackageName})
    #resp = requests.get(url=URL+'/load', params={'name':'datapackage', 'path':'../Data/opsd-weather/' + datapackageName})
    print("loading response: " + (resp.text if resp.ok else str(resp.headers)))
    
    # deserialize the data
    remove_shm_from_resource_tracker()
    datapackage = shared_memory.SharedMemory("datapackage")
    try:
        expr = deserialize(datapackage.buf)
        print(expr)
    finally:
        datapackage.close()
        # request server to unload the data
        resp = requests.get(url=URL+'/unload', params={'name':'datapackage'})
        print("unloading response: " + (resp.text if resp.ok else str(resp.headers)))
    
if __name__ == "__main__":
    main()