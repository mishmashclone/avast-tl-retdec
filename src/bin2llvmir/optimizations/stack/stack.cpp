/**
* @file src/bin2llvmir/optimizations/stack/stack.cpp
* @brief Reconstruct stack.
* @copyright (c) 2017 Avast Software, licensed under the MIT license
*/

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>

#include "retdec/bin2llvmir/analyses/reaching_definitions.h"
#include "retdec/bin2llvmir/optimizations/stack/stack.h"
#include "retdec/bin2llvmir/providers/asm_instruction.h"
#include "retdec/bin2llvmir/utils/ir_modifier.h"
#define debug_enabled true
#include "retdec/bin2llvmir/utils/llvm.h"

using namespace llvm;

namespace retdec {
namespace bin2llvmir {

char StackAnalysis::ID = 0;

static RegisterPass<StackAnalysis> X(
		"stack",
		"Stack optimization",
		false, // Only looks at CFG
		false // Analysis Pass
);

StackAnalysis::StackAnalysis() :
		ModulePass(ID)
{

}

bool StackAnalysis::runOnModule(llvm::Module& m)
{
	_module = &m;
	_config = ConfigProvider::getConfig(_module);
	_abi = AbiProvider::getAbi(_module);
	_dbgf = DebugFormatProvider::getDebugFormat(_module);
	return run();
}

bool StackAnalysis::runOnModuleCustom(
		llvm::Module& m,
		Config* c,
		Abi* abi,
		DebugFormat* dbgf)
{
	_module = &m;
	_config = c;
	_abi = abi;
	_dbgf = dbgf;
	return run();
}

bool StackAnalysis::run()
{
	if (_config == nullptr)
	{
		return false;
	}

	ReachingDefinitionsAnalysis RDA;
	RDA.runOnModule(*_module, _abi);

	IrModifier irModif(_module, _config);

	for (auto& f : *_module)
	{
		std::cout << "HANDLING: " << f.getName().str() << std::endl;

		std::map<Value*, Value*> val2val;
		for (inst_iterator I = inst_begin(f), E = inst_end(f); I != E;)
		{
			Instruction& i = *I;
			++I;

			if (StoreInst *store = dyn_cast<StoreInst>(&i))
			{
				if (AsmInstruction::isLlvmToAsmInstruction(store))
				{
					continue;
				}

				handleInstruction(
						RDA,
						store,
						store->getValueOperand(),
						store->getValueOperand()->getType(),
						val2val);

				if (_abi->isStackPointerRegister(store->getPointerOperand()))
				{
						continue;
				}

				handleInstruction(
						RDA,
						store,
						store->getPointerOperand(),
						store->getValueOperand()->getType(),
						val2val);
			}
			else if (LoadInst* load = dyn_cast<LoadInst>(&i))
			{
				if (_abi->isStackPointerRegister(load->getPointerOperand()))
				{
						continue;
				}

				handleInstruction(
						RDA,
						load,
						load->getPointerOperand(),
						load->getType(),
						val2val);
			}
		}

		for (auto &sv: _config->getStackVariables(&f))
		{
			if (auto* strType = dyn_cast<StructType>(sv->getAllocatedType()))
			{
				irModif.convertToStructure(sv, strType);
			}
		}
	}

	return false;
}


void StackAnalysis::handleInstruction(
		ReachingDefinitionsAnalysis& RDA,
		llvm::Instruction* inst,
		llvm::Value* val,
		llvm::Type* type,
		std::map<llvm::Value*, llvm::Value*>& val2val)
{
	LOG << "Handling instruction: " << llvmObjToString(inst) << std::endl;

	//TODO: what about all globals?
	SymbolicTree root = !_abi->isGeneralPurposeRegister(val) ?
		SymbolicTree(RDA, val, &val2val) : SymbolicTree(RDA, inst);

	LOG << "Root of instruction: " << std::endl << root << std::endl;

	if (!root.isVal2ValMapUsed())
	{
		bool stackPtr = false;
		for (SymbolicTree* n : root.getPostOrder())
		{
			if (_abi->isStackPointerRegister(n->value)
				|| _abi->isStackVariable(n->value))
			{
				stackPtr = true;
				break;
			}
		}
		if (!stackPtr)
		{
			LOG << "===> no SP" << std::endl;
			return;
		}
	}

	auto* debugSv = getDebugStackVariable(inst->getFunction(), root);
	auto* configSv = getConfigStackVariable(inst->getFunction(), root);

	auto* ci = dyn_cast_or_null<ConstantInt>(root.value);
	root.simplifyNode();
	LOG << "Simplified root of instruction: " << std::endl << root << std::endl;

	if (auto* pdSv = getDebugStackVariable(inst->getFunction(), root))
	{
		debugSv = pdSv;
	}
	if (auto* pcSv = getConfigStackVariable(inst->getFunction(), root))
	{
		configSv = pcSv;
	}
	LOG << "Root value: " << llvmObjToString(root.value) << std::endl;
	ci = dyn_cast_or_null<ConstantInt>(root.value);
	
	if (ci == nullptr)
	{
		return;
	}

	if (auto* s = dyn_cast<StoreInst>(inst))
	{
		if (s->getValueOperand() == val)
		{
			val2val[inst] = ci;
		}
	}

	LOG << "\tConstant extracted: " << llvmObjToString(ci) << std::endl;
	LOG << "\tInteger constant  : " << ci->getSExtValue() << std::endl;

	std::string name = "";
	Type* t = type;

	if (debugSv)
	{
		name = debugSv->getName();
		t = llvm_utils::stringToLlvmTypeDefault(_module, debugSv->type.getLlvmIr());
	}
	else if (configSv)
	{
		name = configSv->getName();
		t = llvm_utils::stringToLlvmTypeDefault(_module, configSv->type.getLlvmIr());
	}

	IrModifier irModif(_module, _config);
	auto p = irModif.getStackVariable(
			inst->getFunction(),
			ci->getSExtValue(),
			t,
			name);

	AllocaInst* a = p.first;
	auto* ca = p.second;

	if (debugSv || configSv)
	{
		ca->setRealName(name);
		ca->setIsFromDebug(true);
	}

	LOG << "\tHave stack variable: " << llvmObjToString(a) << std::endl;
	LOG << "\tModifying instrucio: " << llvmObjToString(inst) << std::endl;
	LOG << std::endl;

	auto* s = dyn_cast<StoreInst>(inst);
	auto* l = dyn_cast<LoadInst>(inst);
	if (s && s->getPointerOperand() == val)
	{
		Value* dst = a;
		if (a->getType()->getElementType()->isStructTy())
		{
			// TODO: more levels of structure
			auto* gep =irModif.getElement(dst, 0);
			gep->insertBefore(inst);
			dst = gep;
		}
		auto* conv = IrModifier::convertValueToType(
				s->getValueOperand(),
				dst->getType()->getPointerElementType(),
				inst);
		new StoreInst(conv, dst, inst);
		s->eraseFromParent();
	}
	else if (l && l->getPointerOperand() == val)
	{
		Value* ptr = a;
		if (a->getType()->getElementType()->isStructTy())
		{
			// TODO: more levels of structure
			auto* gep =irModif.getElement(ptr, 0);
			gep->insertBefore(inst);
			ptr = gep;
		}
		auto* nl = new LoadInst(ptr, "", l);
		auto* conv = IrModifier::convertValueToType(nl, l->getType(), l);
		l->replaceAllUsesWith(conv);
		l->eraseFromParent();
	}
	else
	{
		auto* conv = IrModifier::convertValueToType(a, val->getType(), inst);
		inst->replaceUsesOfWith(val, conv);
	}
}

retdec::utils::Maybe<int> StackAnalysis::getBaseOffset(SymbolicTree& root)
{
	retdec::utils::Maybe<int> baseOffset;
	if (auto* ci = dyn_cast_or_null<ConstantInt>(root.value))
	{
		baseOffset = ci->getSExtValue();
	}
	else
	{
		for (SymbolicTree* n : root.getLevelOrder())
		{
			if (isa<AddOperator>(n->value)
					&& n->ops.size() == 2
					&& isa<LoadInst>(n->ops[0].value)
					&& isa<ConstantInt>(n->ops[1].value))
			{
				auto* l = cast<LoadInst>(n->ops[0].value);
				auto* ci = cast<ConstantInt>(n->ops[1].value);
				if (_abi->isRegister(l->getPointerOperand()))
				{
					baseOffset = ci->getSExtValue();
				}
				break;
			}
		}
	}

	return baseOffset;
}

/**
 * Find a value that is being added to the stack pointer register in \p root.
 * Find a debug variable with offset equal to this value.
 */
retdec::config::Object* StackAnalysis::getDebugStackVariable(
		llvm::Function* fnc,
		SymbolicTree& root)
{
	auto baseOffset = getBaseOffset(root);
	if (baseOffset.isUndefined())
	{
		return nullptr;
	}

	if (_dbgf == nullptr)
	{
		return nullptr;
	}

	auto* debugFnc = _dbgf->getFunction(_config->getFunctionAddress(fnc));
	if (debugFnc == nullptr)
	{
		return nullptr;
	}

	for (auto& p : debugFnc->locals)
	{
		auto& var = p.second;
		if (!var.getStorage().isStack())
		{
			continue;
		}
		if (var.getStorage().getStackOffset() == baseOffset)
		{
			return &var;
		}
	}

	return nullptr;
}

retdec::config::Object* StackAnalysis::getConfigStackVariable(
		llvm::Function* fnc,
		SymbolicTree& root)
{
	auto baseOffset = getBaseOffset(root);
	if (baseOffset.isUndefined())
	{
		return nullptr;
	}

	auto cfn = _config->getConfigFunction(fnc);
	if (cfn && _config->getLlvmStackVariable(fnc, baseOffset) == nullptr)
	{
		for (auto& l: cfn->locals)
		{
			if (l.second.getStorage().getStackOffset() == baseOffset)
			{
				return &l.second;
			}
		}
	}

	return nullptr;
}


} // namespace bin2llvmir
} // namespace retdec
