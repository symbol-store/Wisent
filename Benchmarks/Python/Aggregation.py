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

import timeit

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
    
class Symbol:
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return "'" + self.name
    def __repr__(self):
        return self.__str__()

class LazyExpression:
    def __init__(self, offset, args, argTypes, exprs, strings):
        self.__args = args
        self.__argTypes = argTypes
        self.__exprs = exprs
        self.__strings = strings
        #assert ArgType(self.__readArgumentType(offset)) == ArgType.EXPRESSION # cannot check due to RLE
        self.index = self.__readExpressionIndex(offset)
        
    def at(self, childIndex):
        argsGen = self.getArguments()
        for i in range(0, childIndex - 1):
            next(argsGen)
        return next(argsGen)
          
    def getArguments(self):
        head, startChild, endChild = self.__readExpression()
        while(startChild < endChild):
            argType = self.__readArgumentType(startChild)
            if(argType & 0x80):
                argType &= ~0x80
                argCount = self.__readRLELength(startChild)
                for i in range(startChild, startChild + argCount):
                    yield self.__readArgWithType(i, argType)
                startChild += argCount
            else:
                yield self.__readArgWithType(startChild, argType)
                
    def getTypedArguments(self, expectedType):
        head, startChild, endChild = self.__readExpression()
        while(startChild < endChild):
            argType = self.__readArgumentType(startChild)
            if(argType & 0x80):
                argType &= ~0x80
                if expectedType == ArgType(argType):
                    argCount = self.__readRLELength(startChild)
                    for i in range(startChild, startChild + argCount):
                        yield self.__readArgWithType(i, argType)
                startChild += argCount
            else:
                if expectedType == ArgType(argType):
                    yield self.__readArgWithType(startChild, argType)
                startChild += 1
                  
    def getMapLazyValue(self, key):
        for expr in self.getTypedArguments(ArgType.EXPRESSION):
            if(expr.getHead() == key):
                return expr
        assert False, "key '" + key + "' not found"
        
    def getHead(self):
        head, startChild, endChild = self.__readExpression()
        return self.__readString(head)
        
    def __readExpressionIndex(self, offset):
        return struct.unpack("@Q", self.__args[offset*8:(offset+1)*8])[0]
        
    def __readArgumentType(self, offset):
        return struct.unpack("@Q", self.__argTypes[offset*8:(offset+1)*8])[0]
        
    def __readRLELength(self, offset):
        return struct.unpack("@I", self.__argTypes[(offset+1)*8:(offset+2)*8-4])[0]
        
    def __readExpression(self):
        return struct.unpack("@QQQ", self.__exprs[self.index*24:(self.index+1)*24])
        
    def __readArgWithType(self, offset, argType):
        match ArgType(argType):
            case ArgType.BOOL:
                return struct.unpack("@?", self.__args[(offset+1)*8-1:(offset+1)*8])[0]
            case ArgType.LONG:
                return struct.unpack("@Q", self.__args[offset*8:(offset+1)*8])[0]
            case ArgType.DOUBLE:
                return struct.unpack("@d", self.__args[offset*8:(offset+1)*8])[0]
            case ArgType.STRING:
                index = struct.unpack("@Q", self.__args[offset*8:(offset+1)*8])[0]
                return self.__readString(index)
            case ArgType.SYMBOL:
                index = struct.unpack("@Q", self.__args[offset*8:(offset+1)*8])[0]
                return self.__readSymbol(index)
            case ArgType.EXPRESSION:
                return LazyExpression(offset, self.__args, self.__argTypes, self.__exprs, self.__strings)
        
    def __readString(self, offset):
        leftChars = len(self.__strings[offset:])
        partialSize = 32 if leftChars > 32 else leftChars
        partialStr = struct.unpack("@" + str(partialSize) + "s", self.__strings[offset:offset+partialSize])[0]
        concatStr = ctypes.create_string_buffer(partialStr).value.decode("utf-8")
        if(len(concatStr) == partialSize):
            concatStr += self.__readString(offset + partialSize)
        concatStr = concatStr
        return concatStr
        
    def __readSymbol(self, offset):
        str = self.__readString(offset)
        return Symbol(str)
        
def getRoot(buffer):
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
    return LazyExpression(0, args, argTypes, exprs, strings)
    
def getTable(root):
    return root.getMapLazyValue("resources").at(0).getMapLazyValue("Object").getMapLazyValue("path").getMapLazyValue("Table")
               
def getColumn(table, columnName):
    return table.getMapLazyValue(columnName)
    
def aggregate(buffer, columnName):
    root = getRoot(buffer)
    table = getTable(root)
    aggColumn = getColumn(table, columnName)
    agg = 0.0
    for val in aggColumn.getTypedArguments(ArgType.DOUBLE):
        agg += val
    return agg

def main():
    # request server to load data
    URL="http://localhost:3000"
    datapackageName = "datapackage" + datasetSuffix + ".json"
    #resp = requests.get(url=URL+'/load', params={'name':'datapackage', 'path':'../Data/owid-deaths/' + datapackageName})
    resp = requests.get(url=URL+'/load', params={'name':'datapackage', 'path':'../Data/opsd-weather/' + datapackageName})
    print("loading response: " + (resp.text if resp.ok else str(resp.headers)))
    
    #columnName = "Accidents (excl. road) - Death Rates"
    columnName = "GB_temperature"
        
    # deserialize and perform the aggregation
    remove_shm_from_resource_tracker()
    datapackage = shared_memory.SharedMemory("datapackage")
    try:
        print("runtime: {} s".format(timeit.Timer(lambda: aggregate(datapackage.buf, columnName)).timeit(1)))
        print("agg={}".format(aggregate(datapackage.buf, columnName)))
    finally:
        datapackage.close()
        # request server to unload the data
        resp = requests.get(url=URL+'/unload', params={'name':'datapackage'})
        print("unloading response: " + (resp.text if resp.ok else str(resp.headers)))
    
if __name__ == "__main__":
    main()