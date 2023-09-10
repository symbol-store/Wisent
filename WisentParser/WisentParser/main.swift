//
//  main.swift
//  WisentParser
//
//  Created by Holger Pirk on 24/06/2023.
//

import Foundation
import Benchmark
// let RootHeaderSize = 4
// enum ArgumentType : Int64 {
//   case ARGUMENT_TYPE_BOOL = 0,
//   ARGUMENT_TYPE_LONG,
//   ARGUMENT_TYPE_DOUBLE,
//   ARGUMENT_TYPE_STRING,
//   ARGUMENT_TYPE_SYMBOL,
//   ARGUMENT_TYPE_EXPRESSION
// };
// let ARGUMENT_TYPE_LONG = WisentArgumentType.ARGUMENT_TYPE_LONG
// let ARGUMENT_TYPE_DOUBLE = WisentArgumentType.ARGUMENT_TYPE_DOUBLE
// let ARGUMENT_TYPE_STRING = WisentArgumentType.ARGUMENT_TYPE_STRING
// let ARGUMENT_TYPE_SYMBOL = WisentArgumentType.ARGUMENT_TYPE_SYMBOL
// let ARGUMENT_TYPE_BOOL = WisentArgumentType.ARGUMENT_TYPE_BOOL
// let ARGUMENT_TYPE_EXPRESSION = WisentArgumentType.ARGUMENT_TYPE_EXPRESSION
// struct WisentExpression {
//   let symbolNameOffset: UInt64
//   let startChildOffset: UInt64
//   let endChildOffset: UInt64
// };

// func wisentLoad(_ path: UnsafePointer<CChar>!, _ sharedMemoryName: UnsafePointer<CChar>!, _ csvPrefix: UnsafePointer<CChar>!) -> UnsafeMutablePointer<CChar>! {
//     return strdup(path)

// }

struct Buffer {
    let input : UnsafeMutablePointer<Int8>
    @inlinable public var expressionCount: Int {get {input.withMemoryRebound(to: Int64.self, capacity: RootHeaderSize){Int($0[1])}}}
    @inlinable public var argumentCount: Int {get {input.withMemoryRebound(to: Int64.self, capacity: RootHeaderSize){Int($0[0])}}}
    @inlinable public var flattenedArguments : UnsafeMutableBufferPointer<Int64> {
        get { input.withMemoryRebound(to: Int64.self, capacity: argumentCount){
                UnsafeMutableBufferPointer<Int64>(start: $0.advanced(by: RootHeaderSize), count: Int(argumentCount))
            }}
    }
    @inlinable public var flattenedArgumentTypes : UnsafeMutableBufferPointer<WisentArgumentType> {
        get { (flattenedArguments.baseAddress!.advanced(by: argumentCount)
                .withMemoryRebound(to: WisentArgumentType.self, capacity: argumentCount){
                    UnsafeMutableBufferPointer<WisentArgumentType>(start: $0, count: argumentCount)
                })}
    }
    @inlinable public var expressions : UnsafeMutableBufferPointer<WisentExpression> {
        get {(flattenedArgumentTypes
                .baseAddress?.advanced(by: argumentCount)
                .withMemoryRebound(to: WisentExpression.self, capacity: expressionCount){
                    UnsafeMutableBufferPointer<WisentExpression>(start: $0, count: expressionCount)
                })!}
    }
    @inlinable public var stringBuffer : UnsafeMutablePointer<Int8> {
        get {expressions.baseAddress!.advanced(by: expressionCount).withMemoryRebound(to: Int8.self, capacity: 1){$0}}
    }
}
struct ComplexExpression {
    typealias ArgumentRun = LazyMapSequence<(Range<Int>), SerializedExpression>
    typealias TypedArguments = UnfoldSequence<(WisentArgumentType,ArgumentRun), Int>
    let argumentIndex : Int
    let input : Buffer
    @inlinable public var arguments : TypedArguments {
        get {
            let e = input.expressions[Int(input.flattenedArguments[argumentIndex])]
            let length = Int(e.startChildOffset) +
                            ((input.flattenedArgumentTypes[Int(e.startChildOffset)].rawValue & 0x80 != 0)
                             ?Int(input.flattenedArgumentTypes[Int(e.startChildOffset)+1].rawValue) : 0)
            return sequence(state: Int(e.startChildOffset)) { state in
                if state == e.endChildOffset { return nil }
                let result = state < length ?
                    (input.flattenedArgumentTypes[Int(e.startChildOffset)],(Int(e.startChildOffset)..<length).lazy.map{SerializedExpression(argumentIndex: $0, input: input)}) :
                    (input.flattenedArgumentTypes[state],(state..<state+1).lazy.map{SerializedExpression(argumentIndex: $0, input: input)})
                state = max(state+1, length)
                return result
            }
        }
    }
    @inlinable public var flatArguments: FlattenSequence<[ArgumentRun]> {
        get {arguments.map{ $0.1 }.joined()}
    }
    @inlinable public var head : String {
        get {
            String(cString: input.stringBuffer.advanced(by: Int(input.expressions[Int(input.flattenedArguments[argumentIndex])].symbolNameOffset)))
        }
    }
}
enum Atom {
    case Int64(Int64); case Double(Double); case String(String);
    case Complex(ComplexExpression); case Symbol(String)
}
struct SerializedExpression {
    let argumentIndex : Int
    let input : Buffer
    @inlinable public var type : WisentArgumentType {get{WisentArgumentType(rawValue: input.flattenedArgumentTypes[argumentIndex].rawValue & 0x7F)}}
    @inlinable public var asAtom : Atom {
        get {
            switch(type){
            case ARGUMENT_TYPE_STRING:
                return Atom.String(String(cString: input.stringBuffer.advanced(by: Int(input.flattenedArguments[argumentIndex]))))
            case ARGUMENT_TYPE_SYMBOL:
                return Atom.Symbol(String(cString: input.stringBuffer.advanced(by: Int(input.flattenedArguments[argumentIndex]))))
            case ARGUMENT_TYPE_LONG:
                return Atom.Int64(input.flattenedArguments[argumentIndex])
            case ARGUMENT_TYPE_DOUBLE:
                return Atom.Double(input.flattenedArguments.withMemoryRebound(to: Double.self){$0[argumentIndex]})
            case ARGUMENT_TYPE_EXPRESSION:
                return Atom.Complex(ComplexExpression(argumentIndex: argumentIndex, input: input))
            default:
                return Atom.String("unimplemented type")
            }
        }
    }
    @inlinable public var arguments: ComplexExpression.TypedArguments {
        get {
            switch asAtom {
            case .Complex(let c): return c.arguments
            default: return sequence(state: 0){_ in nil}
            }
        }
    }
    @inlinable public var flatArguments: FlattenSequence<[ComplexExpression.ArgumentRun]> {
        get {arguments.map{ $0.1 }.joined()}
    }
}

extension ComplexExpression : Equatable {
    static func == (lhs: ComplexExpression, rhs: ComplexExpression) -> Bool {
        lhs.input.input == rhs.input.input && lhs.argumentIndex == rhs.argumentIndex
    }
}
extension Atom : Equatable {}

extension ComplexExpression : CustomStringConvertible {
    var description: String {
        get {
            "(\(head) \(flatArguments.map{$0.description}.joined(separator: " ")))"
        }
    }
}

extension Atom : CustomStringConvertible {
    var description: String {
        get {
            switch self {
            case .Int64(let i):
                return "\(i)"
            case .Double(let d):
                return "\(d)"
            case .String(let s):
                return "\"\(s)\""
            case .Complex(let c):
                return "\(c)"
            case .Symbol(let s):
                return "'\(s)"
            }
        }
    }
}
extension SerializedExpression : CustomStringConvertible {
    var description: String {
        get {
            asAtom.description
        }
    }

    @inlinable public subscript(head: String) -> [SerializedExpression] {
        arguments.filter{$0.0 == ARGUMENT_TYPE_EXPRESSION}.map{
            $0.1
        }.joined().filter{
            switch $0.asAtom {
            case .Complex(let e):
                return e.head == head
            default:
                return false
            }
        }
    }

    @inlinable public func isExpressionWith(head: String) -> Bool{
        switch asAtom {
        case .Complex(let c): return c.head == head
        default: return false
        }
    }

    @inlinable public var asInt: Int64 {
        get {
            input.flattenedArguments[argumentIndex]
        }
    }

    @inlinable public var asDouble: Double {
        get {
            input.flattenedArguments.withMemoryRebound(to: Double.self){$0[argumentIndex]}
        }
    }

}
extension LazySequence {
    var firstElement: Element? {
        var iterator = self.makeIterator()
        return iterator.next()
    }
}

let packageFile = CommandLine.arguments[CommandLine.arguments.count-1]
var path = URL(filePath: packageFile).pathComponents
path[path.count-1] = ""
CommandLine.arguments = CommandLine.arguments.dropLast(1)

var root = SerializedExpression(argumentIndex: 0, input: Buffer(input: UnsafeMutablePointer<Int8>( wisentLoad(packageFile, "sharedmem2", path.joined(separator: "/"))!)))

func sumUp(run: ComplexExpression.ArgumentRun) -> Int64{
    var agg = run.first!.asInt
    for v in run.dropFirst() {
        agg += v.asInt
    }
    return agg
}

func sumUpFloat(run: ComplexExpression.ArgumentRun) -> Double{
    var agg = run.first!.asDouble
    for v in run.dropFirst() {
        agg += v.asDouble
    }
    return agg
}


func processInts(arguments: ComplexExpression.TypedArguments) -> Int64{
    var agg = Int64(0)
    for run in arguments{
        switch WisentArgumentType(rawValue: run.0.rawValue & 0x7F) {
        case ARGUMENT_TYPE_LONG:
            agg += sumUp(run: run.1)
        case ARGUMENT_TYPE_DOUBLE:
            agg += Int64(sumUpFloat(run: run.1))
        default:
            break
        }
    }

    return agg;
}


func processInts(parent: SerializedExpression) -> Int64{
    return processInts(arguments:parent.arguments)

}

var result = Int64(0)
benchmark("sum values") {
    let path = root["resources"].first!["List"].first!["Object"].first!["path"].first!["Table"].first!
    for _ in 1...100 {
        result += processInts(parent: path["GB_temperature"].first!)
    }
}

Benchmark.main()

print(result)
wisentFree("sharedmem2")
