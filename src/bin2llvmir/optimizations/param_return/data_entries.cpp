/**
* @file src/bin2llvmir/optimizations/param_return/data_entries.cpp
* @brief Data entries for parameter analysis.
* @copyright (c) 2019 Avast Software, licensed under the MIT license
*/

#include <set>
#include <tuple>

#include "retdec/bin2llvmir/optimizations/param_return/data_entries.h"
#include "retdec/bin2llvmir/providers/abi/abi.h"

using namespace llvm;

namespace retdec {
namespace bin2llvmir {

//
//=============================================================================
// ArgumentEntryImpl
//=============================================================================
//

ArgumentEntry::ArgumentEntry(llvm::Type* type, const std::string& name):
	_type(type),
	_name(name)
{

}

std::pair<Value*, Type*> ArgumentEntry::get(Function* fnc, const Abi& a) const
{

	return {getValue(fnc, a), getType(fnc, a)};
}

std::pair<Value*, std::string> ArgumentEntry::get(
		Function* fnc,
		const Abi& a,
		const std::string& suffix,
		const std::string& base) const
{

	return {getValue(fnc, a), getName(fnc, a, suffix, base)};
}

std::string ArgumentEntry::getName(
		Function* fnc,
		const Abi& a,
		const std::string& suffix,
		const std::string& base) const
{
	if (!_name.empty())
	{
		return _name;
	}

	std::string newBase = base.empty() ? "arg":base;

	return createName(fnc, a, suffix, newBase);
}

Value* ArgumentEntry::getValue(Function* fnc, const Abi& a) const
{
	Value* val = fetchArgValue(fnc, a);

	return val ? val : a.getConfig()->getGlobalDummy();
}

bool ArgumentEntry::isDefined(Function* fnc, const Abi& a) const
{
	return fetchArgValue(fnc, a) != nullptr;
}

Type* ArgumentEntry::getType(Function* fnc, const Abi& a) const
{
	if (_type)
	{
		return _type;
	}

	auto* val = getValue(fnc, a);
	assert(val && val->getType()->isPointerTy());
	return val->getType()->getPointerElementType();
}

llvm::Value* ArgumentEntry::fetchArgValue(llvm::Function* fnc, const Abi& a) const
{
	return nullptr;
}

std::string ArgumentEntry::createName(
		llvm::Function* fnc,
		const Abi& a,
		const std::string& suffix,
		const std::string& base) const
{
	return base+suffix;
}

void ArgumentEntry::setType(Type* type)
{
	_type = FunctionType::isValidArgumentType(type) ?
			type : nullptr;
}

DummyArgumentEntry::DummyArgumentEntry(
		llvm::Type* type,
		const std::string& name):
			ArgumentEntry(type, name)
{

}

DummyArgumentEntry::~DummyArgumentEntry()
{

}

template<typename ArgID>
ArgumentEntryImpl<ArgID>::ArgumentEntryImpl(
		ArgID argid,
		llvm::Type* type,
		const std::string& name):
			ArgumentEntry(type, name)
{
	_argid = argid;
}

template<typename ArgID>
ArgumentEntryImpl<ArgID>::~ArgumentEntryImpl()
{

}

template<>
Value* StackArgumentEntry::fetchArgValue(Function* fnc, const Abi& a) const
{
	return a.getConfig()->getLlvmStackVariable(fnc, _argid);
}

template<>
Value* RegisterArgumentEntry::fetchArgValue(Function* fnc, const Abi& a) const
{
	return a.getRegister(_argid);
}

template<>
Value* FunctionArgumentEntry::fetchArgValue(Function* fnc, const Abi& a) const
{
	assert(fnc->arg_size() > _argid);
	return (fnc->arg_begin() + _argid);
}

template<>
Value* ConstantArgumentEntry::fetchArgValue(Function* fnc, const Abi& a) const
{
	return _argid;
}
	
template<>
std::string StackArgumentEntry::createName(
	llvm::Function* fnc,
	const Abi& a,
	const std::string& suffix,
	const std::string& base) const
{
	if (suffix.empty())
	{
		return base+std::to_string(_argid);
	}

	return base+suffix;
}

template<>
std::string RegisterArgumentEntry::createName(
	llvm::Function* fnc,
	const Abi& a,
	const std::string& suffix,
	const std::string& base) const
{
	if (suffix.empty())
	{
		Value* val = getValue(fnc, a);
		return base+val->getName().str();
	}
	return base+suffix;
}

template<>
std::string FunctionArgumentEntry::createName(
	llvm::Function* fnc,
	const Abi& a,
	const std::string& suffix,
	const std::string& base) const
{
	return getValue(fnc, a)->getName().str();
}

template<>
std::string ConstantArgumentEntry::createName(
	llvm::Function* fnc,
	const Abi& a,
	const std::string& suffix,
	const std::string& base) const
{
	return getValue(fnc, a)->getName().str();
}

//
//=============================================================================
// ReturnEntry
//=============================================================================
//

ReturnEntry::ReturnEntry(llvm::ReturnInst* r) :
		_retInst(r)
{
}

void ReturnEntry::addRetStore(llvm::StoreInst* st)
{
	_retStores.push_back(st);

	if (std::find(
		_retValues.begin(),
		_retValues.end(),
		st->getPointerOperand()) != _retValues.end())
	{
		_retValues.push_back(st->getPointerOperand());
	}
}

void ReturnEntry::setRetStores(std::vector<llvm::StoreInst*>&& stores)
{
	_retStores = std::move(stores);

	std::set<Value*> vals;
	for (auto& i: _retStores)
	{
		vals.insert(i->getPointerOperand());
	}

	_retValues.assign(vals.begin(), vals.end());
}

void ReturnEntry::setRetStores(const std::vector<llvm::StoreInst*>& stores)
{
	_retStores = stores;

	std::set<Value*> vals;
	for (auto& i: _retStores)
	{
		vals.insert(i->getPointerOperand());
	}

	_retValues.assign(vals.begin(), vals.end());
}

void ReturnEntry::setRetValues(std::vector<llvm::Value*>&& values)
{
	_retStores.erase(std::remove_if(
		_retStores.begin(),
		_retStores.end(),
		[values](StoreInst* st)
		{
			auto* op = st->getPointerOperand();
			return std::find(
				values.begin(),
				values.end(), op) == values.end();
		}),
	_retStores.end());

	_retValues = std::move(values);
}

void ReturnEntry::setRetValues(const std::vector<llvm::Value*>& values)
{
	_retStores.erase(std::remove_if(
		_retStores.begin(),
		_retStores.end(),
		[values](StoreInst* st)
		{
			auto* op = st->getPointerOperand();
			return std::find(
				values.begin(),
				values.end(), op) == values.end();
		}),
	_retStores.end());

	_retValues = values;
}

ReturnInst* ReturnEntry::getRetInstruction() const
{
	return _retInst;
}

const std::vector<llvm::StoreInst*>& ReturnEntry::retStores() const
{
	return _retStores;
}

const std::vector<llvm::Value*>& ReturnEntry::retValues() const
{
	return _retValues;
}

//
//=============================================================================
// CallableEntry
//=============================================================================
//

bool CallableEntry::isVoidarg() const
{
	return _voidarg;
}

void CallableEntry::addArg(llvm::Value* arg)
{
	_args.push_back(arg);
}

void CallableEntry::setVoidarg(bool voidarg)
{
	_voidarg = voidarg;
}

void CallableEntry::setArgTypes(
		std::vector<Type*>&& types,
		std::vector<std::string>&& names)
{
	_argTypes = std::move(types);
	_argNames = std::move(names);

	if (_argTypes.size() > _argNames.size())
	{
		_argNames.resize(_argTypes.size(), "");
	}
	else if (_argTypes.size() < _argNames.size())
	{
		_argTypes.resize(_argNames.size(), nullptr);
	}

	if (_argTypes.empty())
	{
		setVoidarg();
	}
}

const std::vector<llvm::Value*>& CallableEntry::args() const
{
	return _args;
}

const std::vector<llvm::Type*>& CallableEntry::argTypes() const
{
	return _argTypes;
}

const std::vector<std::string>& CallableEntry::argNames() const
{
	return _argNames;
}

//
//=============================================================================
//  FunctionEntry
//=============================================================================
//

bool FunctionEntry::isVariadic() const
{
	return _variadic;
}

bool FunctionEntry::isWrapper() const
{
	return _wrap != nullptr;
}

void FunctionEntry::addRetEntry(const ReturnEntry& ret)
{
	_retEntries.push_back(ret);
}

ReturnEntry* FunctionEntry::createRetEntry(llvm::ReturnInst* ret)
{
	_retEntries.push_back(ReturnEntry(ret));

	return &(_retEntries.back());
}

void FunctionEntry::setVariadic(bool variadic)
{
	_variadic = variadic;
}

void FunctionEntry::setArgs(std::vector<llvm::Value*>&& args)
{
	_args = std::move(args);
}

void FunctionEntry::setWrappedCall(llvm::CallInst* wrap)
{
	_wrap = wrap;
}

void FunctionEntry::setRetType(llvm::Type* type)
{
	_retType = type;
}

void FunctionEntry::setRetValue(llvm::Value* val)
{
	_retVal = val;
}

void FunctionEntry::setCallingConvention(const CallingConvention::ID& cc)
{
	if (cc == CallingConvention::ID::CC_VOIDARG)
	{
		setVoidarg();
	}
	else
	{
		_callconv = cc;
	}
}

llvm::Type* FunctionEntry::getRetType() const
{
	return _retType;
}

llvm::Value* FunctionEntry::getRetValue() const
{
	return _retVal;
}

llvm::CallInst* FunctionEntry::getWrappedCall() const
{
	return _wrap;
}

CallingConvention::ID FunctionEntry::getCallingConvention() const
{
	return _callconv;
}

const std::vector<ReturnEntry>& FunctionEntry::retEntries() const
{
	return _retEntries;
}

std::vector<ReturnEntry>& FunctionEntry::retEntries()
{
	return _retEntries;
}

//
//=============================================================================
//  CallEntry
//=============================================================================
//

CallEntry::CallEntry(CallInst* call, const FunctionEntry* base) :
	_baseFunction(base),
	_callInst(call)
{
}

void CallEntry::addRetLoad(LoadInst* load)
{
	_retLoads.push_back(load);
	_retValues.push_back(load->getPointerOperand());

	// TODO duplicity and pointer operand?
}

void CallEntry::setFormatString(const std::string &fmt)
{
	_fmtStr = fmt;
}

void CallEntry::setArgStores(std::vector<llvm::StoreInst*>&& stores)
{
	_argStores = std::move(stores);

	std::set<llvm::Value*> vals;
	for (auto& i : _argStores)
	{
		vals.insert(i->getPointerOperand());
	}

	_args.assign(vals.begin(), vals.end());
}

void CallEntry::setArgs(std::vector<Value*>&& args)
{
	_argStores.erase(
		std::remove_if(
			_argStores.begin(),
			_argStores.end(),
			[args](StoreInst* st)
			{
				auto* op = st->getPointerOperand();
				return std::find(
					args.begin(),
					args.end(), op) == args.end();
			}),
		_argStores.end());

	_args = std::move(args);
}

void CallEntry::setRetLoads(std::vector<LoadInst*>&& loads)
{
	_retLoads = std::move(loads);

	std::set<llvm::Value*> vals;
	for (auto& i: _retLoads)
	{
		vals.insert(i->getPointerOperand());
	}
	_retValues.assign(vals.begin(), vals.end());
}

void CallEntry::setRetValues(std::vector<llvm::Value*>&& values)
{
	_retLoads.erase(std::remove_if(
		_retLoads.begin(),
		_retLoads.end(),
		[values](llvm::LoadInst* st)
		{
			auto* op = st->getPointerOperand();
			return std::find(
				values.begin(),
				values.end(), op) == values.end();
		}),
	_retLoads.end());

	_retValues = std::move(values);
}

CallInst* CallEntry::getCallInstruction() const
{
	return _callInst;
}

const FunctionEntry* CallEntry::getBaseFunction() const
{
	return _baseFunction;
}

std::string CallEntry::getFormatString() const
{
	return _fmtStr;
}

const std::vector<llvm::StoreInst*>& CallEntry::argStores() const
{
	return _argStores;
}

const std::vector<Value*>& CallEntry::retValues() const
{
	return _retValues;
}

const std::vector<LoadInst*>& CallEntry::retLoads() const
{
	return _retLoads;
}

//
//=============================================================================
//  DataFlowEntry
//=============================================================================
//

DataFlowEntry::DataFlowEntry(Value* called):
	_calledValue(called)
{
}

bool DataFlowEntry::isFunction() const
{
	return getFunction() != nullptr;
}

bool DataFlowEntry::isValue() const
{
	return _calledValue && !isFunction();
}

bool DataFlowEntry::hasDefinition() const
{
	return isFunction() && !getFunction()->empty();
}

Function* DataFlowEntry::getFunction() const
{
	return dyn_cast_or_null<Function>(_calledValue);
}

Value* DataFlowEntry::getValue() const
{
	return _calledValue;
}

void DataFlowEntry::setCalledValue(llvm::Value* called)
{
	_calledValue = called;
}

CallEntry* DataFlowEntry::createCallEntry(CallInst* call)
{
	_calls.push_back(CallEntry(call, this));
	return &(_calls.back());
}

const std::vector<CallEntry>& DataFlowEntry::callEntries() const
{
	return _calls;
}

std::vector<CallEntry>& DataFlowEntry::callEntries()
{
	return _calls;
}

}
}
