//===-- TriCoreISelDAGToDAG.cpp - A dag to dag inst selector for TriCore --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the TriCore target.
//
//===----------------------------------------------------------------------===//

#include "TriCore.h"
#include "TriCoreTargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "TriCoreInstrInfo.h"
#include "TriCoreCallingConvHook.h"

#define DEBUG_TYPE "tricore-isel"

using namespace llvm;



namespace {
struct TriCoreISelAddressMode {
	enum {
		RegBase,
		FrameIndexBase
	} BaseType;

	struct {            // This is really a union, discriminated by BaseType!
		SDValue Reg;
		int FrameIndex;
	} Base;

	int64_t Disp;
	const GlobalValue *GV;
	const Constant *CP;
	const BlockAddress *BlockAddr;
	const char *ES;
	int JT;
	unsigned Align;    // CP alignment.

	TriCoreISelAddressMode()
	: BaseType(RegBase), Disp(0), GV(nullptr), CP(nullptr),
		BlockAddr(nullptr), ES(nullptr), JT(-1), Align(0) {
	}

	bool hasSymbolicDisplacement() const {
		return GV != nullptr || CP != nullptr || ES != nullptr || JT != -1;
	}

	void dump() {
		errs() << "rriCoreISelAddressMode " << this << '\n';
		if (BaseType == RegBase && Base.Reg.getNode() != nullptr) {
			errs() << "Base.Reg ";
			Base.Reg.getNode()->dump();
		} else if (BaseType == FrameIndexBase) {
			errs() << " Base.FrameIndex " << Base.FrameIndex << '\n';
		}
		errs() << " Disp " << Disp << '\n';
		if (GV) {
			errs() << "GV ";
			GV->dump();
		} else if (CP) {
			errs() << " CP ";
			CP->dump();
			errs() << " Align" << Align << '\n';
		} else if (ES) {
			errs() << "ES ";
			errs() << ES << '\n';
		} else if (JT != -1)
			errs() << " JT" << JT << " Align" << Align << '\n';
	}
};
}

/// TriCoreDAGToDAGISel - TriCore specific code to select TriCore machine
/// instructions for SelectionDAG operations.
///
namespace {
class TriCoreDAGToDAGISel : public SelectionDAGISel {
	const TriCoreSubtarget &Subtarget;


public:
	explicit TriCoreDAGToDAGISel(TriCoreTargetMachine &TM, CodeGenOpt::Level OptLevel)
	: SelectionDAGISel(TM, OptLevel), Subtarget(*TM.getSubtargetImpl()) {}

	SDNode *Select(SDNode *N);

	bool SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset);
	bool SelectAddr_new(SDValue N, SDValue &Base, SDValue &Disp);
	bool MatchAddress(SDValue N, TriCoreISelAddressMode &AM);
	bool MatchWrapper(SDValue N, TriCoreISelAddressMode &AM);
	bool MatchAddressBase(SDValue N, TriCoreISelAddressMode &AM);
	static bool isPointer();
	static bool isInteger();
	virtual const char *getPassName() const {
		return "TriCore DAG->DAG Pattern Instruction Selection";
	}

	static bool ptyType;
	static bool intType;

	// Include the pieces autogenerated from the target description.
#include "TriCoreGenDAGISel.inc"
};

} // end anonymous namespace

bool TriCoreDAGToDAGISel::ptyType = false;
bool TriCoreDAGToDAGISel::intType = false;
bool TriCoreDAGToDAGISel::isPointer() { return ptyType;}
bool TriCoreDAGToDAGISel::isInteger() { return intType;}
/// MatchWrapper - Try to match MSP430ISD::Wrapper node into an addressing mode.
/// These wrap things that will resolve down into a symbol reference.  If no
/// match is possible, this returns true, otherwise it returns false.
bool TriCoreDAGToDAGISel::MatchWrapper(SDValue N, TriCoreISelAddressMode &AM) {
	// If the addressing mode already has a symbol as the displacement, we can
	// never match another symbol.
	if (AM.hasSymbolicDisplacement()) {
		DEBUG(errs().changeColor(raw_ostream::YELLOW,1);
		errs() <<"hasSymbolicDisplacement\n";
		N.dump();
		errs().changeColor(raw_ostream::WHITE,0); );
		return true;
	}

	SDValue N0 = N.getOperand(0);

	DEBUG(errs() << "Match Wrapper N => ";
	N.dump();
	errs()<< "N0 => "; N0.dump(); );


	if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(N0)) {
		AM.GV = G->getGlobal();
		AM.Disp += G->getOffset();
		DEBUG(errs() << "MatchWrapper->Displacement: " << AM.Disp );
		//AM.SymbolFlags = G->getTargetFlags();
	}
	return false;
}

/// MatchAddressBase - Helper for MatchAddress. Add the specified node to the
/// specified addressing mode without any further recursion.
bool TriCoreDAGToDAGISel::MatchAddressBase(SDValue N, TriCoreISelAddressMode &AM) {
	// Is the base register already occupied?
	if (AM.BaseType != TriCoreISelAddressMode::RegBase || AM.Base.Reg.getNode()) {
		// If so, we cannot select it.
		return true;
	}

	// Default, generate it as a register.
	AM.BaseType = TriCoreISelAddressMode::RegBase;
	AM.Base.Reg = N;
	return false;
}


bool TriCoreDAGToDAGISel::MatchAddress(SDValue N, TriCoreISelAddressMode &AM) {
	DEBUG(errs() << "MatchAddress: "; AM.dump());
	DEBUG(errs() << "Node: "; N.dump());


	switch (N.getOpcode()) {
	default: break;
	case ISD::Constant: {

		uint64_t Val = cast<ConstantSDNode>(N)->getSExtValue();
		AM.Disp += Val;
		DEBUG(errs() << "MatchAddress->Disp: " << AM.Disp ;);
		return false;
	}

	case TriCoreISD::Wrapper:
		if (!MatchWrapper(N, AM))
			return false;
		break;

	case ISD::FrameIndex:
		if (AM.BaseType == TriCoreISelAddressMode::RegBase
				&& AM.Base.Reg.getNode() == nullptr) {
			AM.BaseType = TriCoreISelAddressMode::FrameIndexBase;
			AM.Base.FrameIndex = cast<FrameIndexSDNode>(N)->getIndex();
			return false;
		}
		break;

	case ISD::ADD: {
		TriCoreISelAddressMode Backup = AM;
		if (!MatchAddress(N.getNode()->getOperand(0), AM) &&
				!MatchAddress(N.getNode()->getOperand(1), AM))
			return false;
		AM = Backup;
		if (!MatchAddress(N.getNode()->getOperand(1), AM) &&
				!MatchAddress(N.getNode()->getOperand(0), AM))
			return false;
		AM = Backup;

		break;
	}

	case ISD::OR:
		// Handle "X | C" as "X + C" iff X is known to have C bits clear.
		if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
			TriCoreISelAddressMode Backup = AM;
			uint64_t Offset = CN->getSExtValue();
			// Start with the LHS as an addr mode.
			if (!MatchAddress(N.getOperand(0), AM) &&
					// Address could not have picked a GV address for the displacement.
					AM.GV == nullptr &&
					// Check to see if the LHS & C is zero.
					CurDAG->MaskedValueIsZero(N.getOperand(0), CN->getAPIntValue())) {
				AM.Disp += Offset;
				return false;
			}
			AM = Backup;
		}
		break;
	}

	return MatchAddressBase(N, AM);
}

/// SelectAddr - returns true if it is able pattern match an addressing mode.
/// It returns the operands which make up the maximal addressing mode it can
/// match by reference.
bool TriCoreDAGToDAGISel::SelectAddr_new(SDValue N,
		SDValue &Base, SDValue &Disp) {
	TriCoreISelAddressMode AM;

	DEBUG( errs().changeColor(raw_ostream::YELLOW,1);
	N.dump();
	errs().changeColor(raw_ostream::WHITE,0) );


	if (MatchAddress(N, AM))
		return false;

	EVT VT = N.getValueType();
	if (AM.BaseType == TriCoreISelAddressMode::RegBase) {
		DEBUG(errs() << "It's a reg base";);
		if (!AM.Base.Reg.getNode())
			AM.Base.Reg = CurDAG->getRegister(0, VT);
	}


	Base = (AM.BaseType == TriCoreISelAddressMode::FrameIndexBase)
            		 ? CurDAG->getTargetFrameIndex(
            				 AM.Base.FrameIndex,
										 getTargetLowering()->getPointerTy(CurDAG->getDataLayout()))
            				 : AM.Base.Reg;

	if (AM.GV) {
		DEBUG(errs() <<"AM.GV" );
		//GlobalAddressSDNode *gAdd = dyn_cast<GlobalAddressSDNode>(N.getOperand(0));
		Base = N;
		Disp = CurDAG->getTargetConstant(AM.Disp, N, MVT::i32);
	}
	else {
		DEBUG(errs()<<"SelectAddr -> AM.Disp\n";
		errs()<< "SelectAddr -> Displacement: " << AM.Disp; );
		Disp = CurDAG->getTargetConstant(AM.Disp, SDLoc(N), MVT::i32);
	}


	return true;
}


bool TriCoreDAGToDAGISel::SelectAddr(SDValue Addr, SDValue &Base, SDValue &Offset) {


	return SelectAddr_new(Addr, Base, Offset);

	outs().changeColor(raw_ostream::GREEN,1);
	Addr.dump();
	outs() <<"Addr Opcode: " << Addr.getOpcode() <<"\n";
	outs().changeColor(raw_ostream::WHITE,0);


	if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
		//    EVT PtrVT = getTargetLowering()->getPointerTy(*TM.getDataLayout());
		EVT PtrVT = getTargetLowering()->getPointerTy(CurDAG->getDataLayout());
		Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), PtrVT);
		Offset = CurDAG->getTargetConstant(0, Addr, MVT::i32);
		//    outs().changeColor(raw_ostream::RED)<<"Selecting Frame!\n";
		//    outs().changeColor(raw_ostream::WHITE);

		return true;
	}


	outs().changeColor(raw_ostream::BLUE,1);
	Addr.dump();
	outs().changeColor(raw_ostream::WHITE,0);

	if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
			Addr.getOpcode() == ISD::TargetGlobalAddress ||
			Addr.getOpcode() == ISD::TargetGlobalTLSAddress) {
		outs()<<"This is working!!!!!!!!!!!!!!\n";
		//Base = Addr;
		//Offset = CurDAG->getTargetConstant(gAdd->getOffset(), Addr, MVT::i32);
		return false;
	}

	Base = Addr;
	Offset = CurDAG->getTargetConstant(0, Addr, MVT::i32);
	return true;
}

SDNode *TriCoreDAGToDAGISel::Select(SDNode *N) {

	SDLoc dl(N);
	// Dump information about the Node being selected
	DEBUG(errs().changeColor(raw_ostream::GREEN) << "Selecting: ");
	DEBUG(N->dump(CurDAG));
	DEBUG(errs() << "\n");
	switch (N->getOpcode()) {
	case ISD::FrameIndex: {
		//FrameIndexSDNode *FSDNode = cast<FrameIndexSDNode>(N);
		//outs() <<"Func Name: " <<MF->getFunction()->getName() <<"\n";
		int FI = cast<FrameIndexSDNode>(N)->getIndex();
		//N->dump(CurDAG);
		SDValue TFI = CurDAG->getTargetFrameIndex(FI, MVT::i32);
		if (N->hasOneUse()) {
			return CurDAG->SelectNodeTo(N, TriCore::ADDrc, MVT::i32, TFI,
					CurDAG->getTargetConstant(0, dl, MVT::i32));
		}
		return CurDAG->getMachineNode(TriCore::ADDrc, dl, MVT::i32, TFI,
				CurDAG->getTargetConstant(0, dl, MVT::i32));
	}
	case TriCoreISD::SUB: {
		SDValue op1 = N->getOperand(0);
		SDValue zeroConst = CurDAG->getTargetConstant(0, dl, MVT::i32);
		if (N->hasOneUse()) {
			return CurDAG->SelectNodeTo(N, TriCore::RSUBsr, MVT::i32,
					op1, zeroConst);
		}
		return CurDAG->getMachineNode(TriCore::RSUBsr, dl, MVT::i32,
				op1, zeroConst);
	}
	case ISD::STORE: {
		//StoreSDNode *SD = cast<StoreSDNode>(N);
		//outs()<< "getSizeInBits: " << SD->getMemoryVT().getSizeInBits() << "\n";
		//outs()<< "getAlignment: " << SD->getAlignment() << "\n";
		ptyType = false;
		ptyType = (N->getOperand(1)->getArgType() == (int64_t)MVT::iPTR) ?
				true : false;
		break;
	}
	case ISD::LOAD:{
		LoadSDNode *LD = cast<LoadSDNode>(N);
		LD->dump();
		outs()<<"LD getAlignment: " << LD->getAlignment() << "\n";
		outs()<<"LD getOpcode: " << LD->getOpcode() << "\n";
		outs()<<"LD getNumOp: " << LD->getNumOperands() << "\n";
		outs()<<"LD getExtensionType: " << (int)LD->getExtensionType() << "\n";
		outs()<<"LD getEVTString: " << LD->getMemoryVT().getEVTString() << "\n";
		outs()<<"LD getOriginalAlignment: " << LD->getOriginalAlignment() << "\n";
		outs()<<"LD HasDebugValue: " << LD->getHasDebugValue() << "\n";
//		LD->getOperand(0).dump();
//		LD->getOperand(1).dump();
//		LD->getOperand(2).dump();
		SDValue chain =  LD->getChain();
		chain.dump();
		//N->dump();
		break;
	}
	case ISD::SEXTLOAD: {
		outs()<<"Signextend\n";
		outs()<<"LD getNumOp: " << N->getNumOperands() << "\n";
		break;
	}
}

		SDNode *ResNode = SelectCode(N);

	DEBUG(errs() << "=> ");
	if (ResNode == nullptr || ResNode == N)
		DEBUG(N->dump(CurDAG));
	else
		DEBUG(ResNode->dump(CurDAG));
	DEBUG(errs() << "\n");
	return ResNode;
}
/// createTriCoreISelDag - This pass converts a legalized DAG into a
/// TriCore-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createTriCoreISelDag(TriCoreTargetMachine &TM,
		CodeGenOpt::Level OptLevel) {
	return new TriCoreDAGToDAGISel(TM, OptLevel);
}
