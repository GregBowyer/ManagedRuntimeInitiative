/*
 * Copyright 1998-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "assembler_pd.hpp"
#include "block.hpp"
#include "cfgnode.hpp"
#include "codeBlob.hpp"
#include "debug.hpp"
#include "output.hpp"
#include "memnode.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

#ifndef PRODUCT
#define DEBUG_ARG(x) , x
#else
#define DEBUG_ARG(x)
#endif

//------------------------------Output-----------------------------------------
// Convert Nodes to instruction bits and pass off to the VM
void Compile::Output() {
  // RootNode goes
  assert( _cfg->_broot->_nodes.size() == 0, "" );

  // Make sure I can find the Start Node
  Block_Array& bbs = _cfg->_bbs;
  Block *entry = _cfg->_blocks[1];
  Block *broot = _cfg->_broot;

  const StartNode *start = entry->_nodes[0]->as_Start();
  if ( !start ) {
    _cfg->C->record_failure("start node is null", false);
    return;
  }

  // Replace StartNode with prolog
  MachPrologNode *prolog = new (this) MachPrologNode();
  entry->_nodes.map( 0, prolog );
  bbs.map( prolog->_idx, entry );
  bbs.map( start->_idx, NULL ); // start is no longer in any block
  
  // Break before main entry point
if(_method->break_at_execute()
#ifndef PRODUCT
      || (C2Breakpoint )
      || (OptoBreakpointOSR && is_osr_compilation())
#endif
    ) {
    _cfg->insert( entry, 1, new (this) MachBreakpointNode() );
  }

  // Insert epilogs before every return
  for( uint i=0; i<_cfg->_num_blocks; i++ ) { 
    Block *b = _cfg->_blocks[i];
    if( !b->is_connector() && b->non_connector_successor(0) == _cfg->_broot ) { // Found a program exit point?
      Node *m = b->end();
      if( m->is_Mach() && m->as_Mach()->ideal_Opcode() != Op_Halt ) {
        MachEpilogNode *epilog = new (this) MachEpilogNode(m->as_Mach()->ideal_Opcode() == Op_Return);
        b->add_inst( epilog );
        bbs.map(epilog->_idx, b);
        //_regalloc->set_bad(epilog->_idx); // Already initialized this way.
      }
    }
  }

  ScheduleAndBundle();

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output()) {
C2OUT->print("\n---- After ScheduleAndBundle ----\n");
    for (uint i = 0; i < _cfg->_num_blocks; i++) {
C2OUT->print("\nBB#%03d:\n",i);
      Block *bb = _cfg->_blocks[i];
      for (uint j = 0; j < bb->_nodes.size(); j++) {
        Node *n = bb->_nodes[j];
OptoReg::Name reg=_regalloc->get_reg(n);
C2OUT->print(" %-6s ",reg>=0&&reg<REG_COUNT?Matcher::regName[reg]:"");
        n->dump();
      }
    }
  }
#endif

  if (failing())  return;

  BuildOopMaps();

  if (failing())  return;

  // Compute max size for oopmaps
  int framesize_words = _regalloc->_framesize - _regalloc->C->in_preserve_stack_slots();
int max_inarg_words=OptoReg::reg2stack(_regalloc->_matcher._new_SP);
  VReg::VR vreg = OptoReg::as_VReg(_regalloc->_max_reg, framesize_words, max_inarg_words);
  assert0( !_masm.oopmapBuilder() );
  _masm.set_oopmapBuilder(vreg);

  Fill_buffer();

if(method()->break_c2_after_codegen()){
C2OUT->print("### [c2] Breaking at end of codegen: ");
method()->print_short_name(C2OUT);
C2OUT->cr();
    BREAKPOINT;
  }
}

// Determine if this node starts a bundle
bool Compile::starts_bundle(const Node *n) const {
  return (_node_bundling_limit > n->_idx &&
          _node_bundling_base[n->_idx].starts_bundle());
}

// helper for Fill_buffer bailout logic
static void turn_off_compiler(Compile* C) {
  if (CodeCache::unallocated_capacity() >= CodeCacheMinimumFreeSpace*10) {
    // Do not turn off compilation if a single giant method has
    // blown the code cache size.
C->record_failure("excessive request to CodeCache",false);
  } else {
    UseInterpreter            = true;
UseC1=false;
UseC2=false;
C->record_failure("CodeCache is full",false);
    warning("CodeCache is full. Compiling has been disabled");
  }
}


//------------------------------Fill_buffer------------------------------------
void Compile::Fill_buffer() {
  if( root() == NULL ) return;  // broken program?

  // Create an array of unused labels, one for each basic block
  Label *blk_labels = NEW_RESOURCE_ARRAY(Label, _cfg->_num_blocks+1);
  // Bulk init all Labels
  memset(blk_labels,-1,sizeof(Label)*(_cfg->_num_blocks+1));

  // Start of possibly-faulting ops
uint*npe_ops=NEW_RESOURCE_ARRAY(uint,_cfg->_num_blocks+1);

  // Record of any jump-tables
  Label* tmpx = new Label();  
  GrowableArray<Label> jumptables(node_arena(),0,0,*tmpx);
  int jmpnum=0;

  for( uint i=0; i < _cfg->_num_blocks; i++ ) {
    _masm.bind(blk_labels[i]);
    Block *b = _cfg->_blocks[i];
    Node *head = b->head();
    uint last_inst = b->_nodes.size();
    // Emit block normally, except for last instruction.
    for( uint j = 0; j<last_inst; j++ ) {
      Node* n = b->_nodes[j];

      if( n->is_Mach() ) {
        MachNode *mach = n->as_Mach();
if(mach->is_MachNullCheck()){
          // Nothing special for now
}else if(mach->is_Branch()){
          if ( mach->ideal_Opcode() == Op_Jump ) {
            Label* tmp = new Label();;
            jumptables.push(*tmp); // Push a label for the jumptable start
            mach->label_set(jumptables.adr_at(jmpnum++));
          } else { 
            // This requires the TRUE branch target be in succs[0].  FastLock
            // is a 'no_flip_branch' which requires the default fall-thru in
            // succs[0] (and thus the target in succs[1]).
int op=mach->ideal_Opcode();
            bool is_noflip = (op == Op_FastLock) || (op == Op_FastUnlock);
            uint block_num = b->non_connector_successor(is_noflip ? 1 : 0)->_pre_order;
            mach->label_set( &blk_labels[block_num] );
          }
        } else {
          // Record start of this instruction, in case it can fault
          // and we need to track the right NPE handler.
          npe_ops[i] = _masm.rel_pc();
        }
      }
      // Emit just means 'call the assembler'
      n->emit(_regalloc);
    }
  }

  _masm.pre_bake(); // append slow-path code for various compiler bits

_masm.code_ends_data_begins();

  // Fill in the NullCheck tables and the Jump tables and the Exception tables.
  jmpnum=0;
  for( uint i=0; i < _cfg->_num_blocks; i++ ) {
    Block *const b = _cfg->_blocks[i];
    const int sz = b->_nodes.size();
    if( !sz ) continue;
    Node *const n = b->_nodes[sz-1];

    if( n->is_MachNullCheck() ) {
uint block_num=b->non_connector_successor(1)->_pre_order;
      int dst = blk_labels[block_num].rel_pc(&_masm);
      _masm.add_implicit_exception(npe_ops[i],dst);
    } 

const Node*jmp=n->is_block_proj();
    if( jmp && jmp->as_Mach()->ideal_Opcode() == Op_Jump ) {
      // Emit jump table.
      _masm.align(8); // Jump table holds 64-bit PCs for X86 jmp8, even tho all PCs are 32bits
      _masm.bind( *jumptables.adr_at(jmpnum++) );
      // Sort by projection number which is also 'switch' case label.  Build a
      // table of targets mapping case-label to block number.
      int *bnums = NEW_RESOURCE_ARRAY(int,b->_num_succs);
      bzero(bnums,sizeof(int)*b->_num_succs);
      for( uint h = 0; h < b->_num_succs; h++ ) {
        Block* succs_block = b->_succs[h];
        for( uint j = 1; j < succs_block->num_preds(); j++) {
          Node* jpn = succs_block->pred(j);
if(jpn->is_JumpProj()&&jpn->in(0)==jmp){
            uint block_num = succs_block->non_connector()->_pre_order;
uint pnum=jpn->as_JumpProj()->proj_no();
            assert0( 0 <= pnum && 0 < b->_num_succs ); // indices are in table bounds
            assert0( bnums[pnum] == 0 ); // no double assignment
            bnums[pnum] = block_num;
          }
        }
      }

      // now emit 32-bit pcs in a table for each jump target
      for( uint h = 0; h < b->_num_succs; h++ ) {
        Label *blkLabel = &blk_labels[bnums[h]];
        blkLabel->add_jmp(&_masm);
        _masm.emit8(999);       // Patchable 8-byte jump target
      }
    }

    // Calls can throw exceptions which have handlers.  C2 treats calls as
    // multi-way branches with a fast fall-through case, plus a series of
    // branch targets that are reached via throwing the associated exceptions.
    // Build the table mapping the handler's bci (itself determined when the
    // runtime looks up the exception type in the exception tables) to a PC.
    // The BCI is part of the CatchProj.
    if( n->is_Catch() ) {
      const int rel_pc = blk_labels[_cfg->_blocks[i+1]->_pre_order].rel_pc(&_masm);
      DebugScopeBuilder *dsb = _masm.get_dbg(rel_pc);

      for( uint h = 0; h < b->_num_succs; h++ ) {
        Block* succs_block = b->_succs[h];
        for( uint j = 1; j < succs_block->num_preds(); j++) {
Node*pj=succs_block->pred(j);
if(pj->is_CatchProj()&&pj->in(0)==n){
const CatchProjNode*cpn=pj->as_CatchProj();
if(cpn->_con==CatchProjNode::fall_through_index){
              // Assert fall-through index is the next block.
              assert0( succs_block->non_connector() == _cfg->_blocks[i+1] );
            } else {
              const int bci = cpn->handler_bci();
              dsb->add_exception(&_masm,bci,&blk_labels[succs_block->non_connector()->_pre_order]);
            }
          }
        }
      }
    }
  }
}

// Static Variables
#ifndef PRODUCT
uint Scheduling::_total_nop_size = 0;
uint Scheduling::_total_method_size = 0;
uint Scheduling::_total_branches = 0;
uint Scheduling::_total_instructions_per_bundle[Pipeline::_max_instrs_per_cycle+1];
#endif

// Initializer for class Scheduling

Scheduling::Scheduling(ResourceArea*arena,Compile&compile)
  : _arena(arena), 
    _cfg(compile.cfg()), 
    _bbs(compile.cfg()->_bbs), 
    _regalloc(compile.regalloc()),
    _reg_node(arena),
    _bundle_instr_count(0),
    _bundle_cycle_number(0),
    _scheduled(arena),
    _available(arena),
    _next_node(NULL),
    _bundle_use(0, 0, resource_count, &_bundle_use_elements[0]),
    _pinch_free_list(arena)
#ifndef PRODUCT
  , _branches(0)
#endif
{
  // Create a MachNopNode
  _nop = new (&compile) MachNopNode();

  // Now that the nops are in the array, save the count
  // (but allow entries for the nops)
  _node_bundling_limit = compile.unique();
  uint node_max = _regalloc->node_regs_max_index();

  compile.set_node_bundling_limit(_node_bundling_limit);

  // This one is persistant within the Compile class
  _node_bundling_base = NEW_ARENA_ARRAY(compile.comp_arena(), Bundle, node_max);

  // Allocate space for fixed-size arrays
  _node_latency    = NEW_ARENA_ARRAY(arena, unsigned short, node_max);
  _uses            = NEW_ARENA_ARRAY(arena, short,          node_max);
  _current_latency = NEW_ARENA_ARRAY(arena, unsigned short, node_max);

  // Clear the arrays
  memset(_node_bundling_base, 0, node_max * sizeof(Bundle));
  memset(_node_latency,       0, node_max * sizeof(unsigned short));
  memset(_uses,               0, node_max * sizeof(short));
  memset(_current_latency,    0, node_max * sizeof(unsigned short));

  // Clear the bundling information
  memcpy(_bundle_use_elements,
    Pipeline_Use::elaborated_elements,
    sizeof(Pipeline_Use::elaborated_elements));

  // Get the last node
  Block *bb = _cfg->_blocks[_cfg->_blocks.size()-1];

  _next_node = bb->_nodes[bb->_nodes.size()-1];
}

#ifndef PRODUCT
// Scheduling destructor
Scheduling::~Scheduling() {
  _total_branches             += _branches;
}
#endif

// Step ahead "i" cycles
void Scheduling::step(uint i) {

  Bundle *bundle = node_bundling(_next_node);
  bundle->set_starts_bundle();

  // Update the bundle record, but leave the flags information alone
  if (_bundle_instr_count > 0) {
    bundle->set_instr_count(_bundle_instr_count);
    bundle->set_resources_used(_bundle_use.resourcesUsed());
  }

  // Update the state information
  _bundle_instr_count = 0;
  _bundle_cycle_number += i;
  _bundle_use.step(i);
}

void Scheduling::step_and_clear() {
  Bundle *bundle = node_bundling(_next_node);
  bundle->set_starts_bundle();

  // Update the bundle record
  if (_bundle_instr_count > 0) {
    bundle->set_instr_count(_bundle_instr_count);
    bundle->set_resources_used(_bundle_use.resourcesUsed());

    _bundle_cycle_number += 1;
  }

  // Clear the bundling information
  _bundle_instr_count = 0;
  _bundle_use.reset();

  memcpy(_bundle_use_elements,
    Pipeline_Use::elaborated_elements,
    sizeof(Pipeline_Use::elaborated_elements));
}

//------------------------------ScheduleAndBundle------------------------------
// Perform instruction scheduling and bundling over the sequence of
// instructions in backwards order.
void Compile::ScheduleAndBundle() {

  // Don't optimize this if it isn't a method
  if (!_method)
    return;

  // Don't optimize this if scheduling is disabled
  if (!OptoSchedulingPost)
    return;

  NOT_PRODUCT( TracePhase t2("isched", &_t_instrSched, TimeCompiler); )

  // Create a data structure for all the scheduling information
  Scheduling scheduling(Thread::current()->resource_area(), *this);

  // Walk backwards over each basic block, computing the needed alignment
  // Walk over all the basic blocks
  scheduling.DoScheduling();
}

//------------------------------ComputeLocalLatenciesForward-------------------
// Compute the latency of all the instructions.  This is fairly simple,
// because we already have a legal ordering.  Walk over the instructions
// from first to last, and compute the latency of the instruction based
// on the latency of the preceeding instruction(s).
void Scheduling::ComputeLocalLatenciesForward(const Block *bb) {
#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("# -> ComputeLocalLatenciesForward\n");
#endif

  // Walk over all the schedulable instructions
  for( uint j=_bb_start; j < _bb_end; j++ ) {

    // This is a kludge, forcing all latency calculations to start at 1.
    // Used to allow latency 0 to force an instruction to the beginning
    // of the bb
    uint latency = 1;
    Node *use = bb->_nodes[j];
    uint nlen = use->len();

    // Walk over all the inputs
    for ( uint k=0; k < nlen; k++ ) {
      Node *def = use->in(k);
      if (!def)
        continue;

      uint l = _node_latency[def->_idx] + use->latency(k);
      if (latency < l)
        latency = l;
    }

    _node_latency[use->_idx] = latency;

#ifndef PRODUCT
    if (_cfg->C->trace_opto_output()) {
C2OUT->print("# latency %4d: ",latency);
      use->dump();
    }
#endif
  }

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("# <- ComputeLocalLatenciesForward\n");
#endif

} // end ComputeLocalLatenciesForward

// See if this node fits into the present instruction bundle
bool Scheduling::NodeFitsInBundle(Node *n) {
  uint n_idx = n->_idx;

  // If the node cannot be scheduled this cycle, skip it
  if (_current_latency[n_idx] > _bundle_cycle_number) {
#ifndef PRODUCT
    if (_cfg->C->trace_opto_output())
C2OUT->print("#     NodeFitsInBundle [%4d]: FALSE; latency %4d > %d\n",
        n->_idx, _current_latency[n_idx], _bundle_cycle_number);
#endif
    return (false);
  }

  const Pipeline *node_pipeline = n->pipeline();

  uint instruction_count = node_pipeline->instructionCount();
  Unimplemented();
  //if (node_pipeline->mayHaveNoCode() && n->size(_regalloc) == 0)
  //  instruction_count = 0;
  //else if (node_pipeline->hasBranchDelay() && !_unconditional_delay_slot)
  //  instruction_count++;

  if (_bundle_instr_count + instruction_count > Pipeline::_max_instrs_per_cycle) {
#ifndef PRODUCT
    if (_cfg->C->trace_opto_output())
C2OUT->print("#     NodeFitsInBundle [%4d]: FALSE; too many instructions: %d > %d\n",
        n->_idx, _bundle_instr_count + instruction_count, Pipeline::_max_instrs_per_cycle);
#endif
    return (false);
  }

  // Don't allow non-machine nodes to be handled this way
  if (!n->is_Mach() && instruction_count == 0)
    return (false);

  // See if there is any overlap
  uint delay = _bundle_use.full_latency(0, node_pipeline->resourceUse());

  if (delay > 0) {
#ifndef PRODUCT
    if (_cfg->C->trace_opto_output())
C2OUT->print("#     NodeFitsInBundle [%4d]: FALSE; functional units overlap\n",n_idx);
#endif
    return false;
  }

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("#     NodeFitsInBundle [%4d]:  TRUE\n",n_idx);
#endif

  return true;
}

Node * Scheduling::ChooseNodeToBundle() {
  uint siz = _available.size();

  if (siz == 0) {

#ifndef PRODUCT
    if (_cfg->C->trace_opto_output())
C2OUT->print("#   ChooseNodeToBundle: NULL\n");
#endif
    return (NULL);
  }

  // Fast path, if only 1 instruction in the bundle
  if (siz == 1) {
#ifndef PRODUCT
    if (_cfg->C->trace_opto_output()) {
C2OUT->print("#   ChooseNodeToBundle (only 1): ");
      _available[0]->dump();
    }
#endif
    return (_available[0]);
  }

  // Don't bother, if the bundle is already full
  if (_bundle_instr_count < Pipeline::_max_instrs_per_cycle) {
    for ( uint i = 0; i < siz; i++ ) {
      Node *n = _available[i];

      // Skip projections, we'll handle them another way
      if (n->is_Proj())
        continue;

      // This presupposed that instructions are inserted into the
      // available list in a legality order; i.e. instructions that
      // must be inserted first are at the head of the list
      if (NodeFitsInBundle(n)) {
#ifndef PRODUCT
        if (_cfg->C->trace_opto_output()) {
C2OUT->print("#   ChooseNodeToBundle: ");
          n->dump();
        }
#endif
        return (n);
      }
    }
  }

  // Nothing fits in this bundle, choose the highest priority
#ifndef PRODUCT
  if (_cfg->C->trace_opto_output()) {
C2OUT->print("#   ChooseNodeToBundle: ");
    _available[0]->dump();
  }
#endif

  return _available[0];
}

//------------------------------AddNodeToAvailableList-------------------------
void Scheduling::AddNodeToAvailableList(Node *n) {
  assert( !n->is_Proj(), "projections never directly made available" );
  if( _cfg->C->top() == n ) return; // Never schedule 'top'
#ifndef PRODUCT
  if (_cfg->C->trace_opto_output()) {
C2OUT->print("#   AddNodeToAvailableList: ");
    n->dump();
  }
#endif

  int latency = _current_latency[n->_idx];

  // Insert in latency order (insertion sort)
  uint i;
  for ( i=0; i < _available.size(); i++ )
    if (_current_latency[_available[i]->_idx] > latency)
      break;

  // Special Check for compares following branches
  if( n->is_Mach() && _scheduled.size() > 0 ) {
    int op = n->as_Mach()->ideal_Opcode();
    Node *last = _scheduled[0];
    if( last->is_MachIf() && last->in(1) == n &&
        ( op == Op_CmpI ||
          op == Op_CmpU ||
          op == Op_CmpP ||
          op == Op_CmpF ||
          op == Op_CmpD ||
          op == Op_CmpL ) ) {

      // Recalculate position, moving to front of same latency
      for ( i=0 ; i < _available.size(); i++ )
        if (_current_latency[_available[i]->_idx] >= latency)
          break;
    }
  }

  // Insert the node in the available list
  _available.insert(i, n);

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
    dump_available();
#endif
}

//------------------------------DecrementUseCounts-----------------------------
void Scheduling::DecrementUseCounts(Node *n, const Block *bb) {
  for ( uint i=0; i < n->len(); i++ ) {
    Node *def = n->in(i);
    if (!def) continue;
    if( def->is_Proj() )        // If this is a machine projection, then 
      def = def->in(0);         // propagate usage thru to the base instruction

    if( _bbs[def->_idx] != bb ) // Ignore if not block-local
      continue;                 

    // Compute the latency
    uint l = _bundle_cycle_number + n->latency(i);
    if (_current_latency[def->_idx] < l)
      _current_latency[def->_idx] = l;

    // If this does not have uses then schedule it
    if ((--_uses[def->_idx]) == 0)
      AddNodeToAvailableList(def);
  }
}

//------------------------------AddNodeToBundle--------------------------------
void Scheduling::AddNodeToBundle(Node *n, const Block *bb) {
#ifndef PRODUCT
  if (_cfg->C->trace_opto_output()) {
C2OUT->print("#   AddNodeToBundle: ");
    n->dump();
  }
#endif

  // Remove this from the available list
  uint i;
  for (i = 0; i < _available.size(); i++)
    if (_available[i] == n)
      break;
  assert(i < _available.size(), "entry in _available list not found");
  _available.remove(i);

  // See if this fits in the current bundle
  const Pipeline *node_pipeline = n->pipeline();
  const Pipeline_Use& node_usage = node_pipeline->resourceUse();

  // Get the number of instructions
  uint instruction_count = node_pipeline->instructionCount();
  Unimplemented();
  //if (node_pipeline->mayHaveNoCode() && n->size(_regalloc) == 0)
  //  instruction_count = 0;

  // Compute the latency information
  uint delay = 0;

  if (instruction_count > 0 || !node_pipeline->mayHaveNoCode()) {
    int relative_latency = _current_latency[n->_idx] - _bundle_cycle_number;
    if (relative_latency < 0)
      relative_latency = 0;

    delay = _bundle_use.full_latency(relative_latency, node_usage);

    // Does not fit in this bundle, start a new one
    if (delay > 0) {
      step(delay);

#ifndef PRODUCT
      if (_cfg->C->trace_opto_output())
C2OUT->print("#  *** STEP(%d) ***\n",delay);
#endif
    }
  }

  // If this was placed in the delay slot, ignore it
  if (true/*n != _unconditional_delay_slot*/) {

    if (delay == 0) {
      if (node_pipeline->hasMultipleBundles()) {
#ifndef PRODUCT
        if (_cfg->C->trace_opto_output())
C2OUT->print("#  *** STEP(multiple instructions) ***\n");
#endif
        step(1);
      }

      else if (instruction_count + _bundle_instr_count > Pipeline::_max_instrs_per_cycle) {
#ifndef PRODUCT
        if (_cfg->C->trace_opto_output())
C2OUT->print("#  *** STEP(%d >= %d instructions) ***\n",
            instruction_count + _bundle_instr_count,
            Pipeline::_max_instrs_per_cycle);
#endif
        step(1);
      }
    }

    // Set the node's latency
    _current_latency[n->_idx] = _bundle_cycle_number;

    // Now merge the functional unit information
    if (instruction_count > 0 || !node_pipeline->mayHaveNoCode())
      _bundle_use.add_usage(node_usage);

    // Increment the number of instructions in this bundle
    _bundle_instr_count += instruction_count;

    // Remember this node for later
    if (n->is_Mach())
      _next_node = n;
  }

  // It's possible to have a BoxLock in the graph and in the _bbs mapping but
  // not in the bb->_nodes array.  This happens for debug-info-only BoxLocks.
  // 'Schedule' them (basically ignore in the schedule) but do not insert them
  // into the block.  All other scheduled nodes get put in the schedule here.
  int op = n->Opcode();
  if( (op == Op_Node && n->req() == 0) || // anti-dependence node OR
(op!=Op_Node//Not an unused antidependence node and
)){

    // Push any trailing projections
    if( bb->_nodes[bb->_nodes.size()-1] != n ) {
      for (DUIterator_Fast imax, i = n->fast_outs(imax); i < imax; i++) {
        Node *foi = n->fast_out(i);
        if( foi->is_Proj() )
          _scheduled.push(foi);
      }
    }
  
    // Put the instruction in the schedule list
    _scheduled.push(n);
  }

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
    dump_available();
#endif

  // Walk all the definitions, decrementing use counts, and
  // if a definition has a 0 use count, place it in the available list.
  DecrementUseCounts(n,bb);
}

//------------------------------ComputeUseCount--------------------------------
// This method sets the use count within a basic block.  We will ignore all
// uses outside the current basic block.  As we are doing a backwards walk,
// any node we reach that has a use count of 0 may be scheduled.  This also
// avoids the problem of cyclic references from phi nodes, as long as phi
// nodes are at the front of the basic block.  This method also initializes
// the available list to the set of instructions that have no uses within this
// basic block.
void Scheduling::ComputeUseCount(const Block *bb) {
#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("# -> ComputeUseCount\n");
#endif

  // Clear the list of available and scheduled instructions, just in case
  _available.clear();
  _scheduled.clear();

#ifdef ASSERT
  for( uint i=0; i < bb->_nodes.size(); i++ )
    assert( _uses[bb->_nodes[i]->_idx] == 0, "_use array not clean" );
#endif

  // Force the _uses count to never go to zero for unscheduable pieces
  // of the block
  for( uint k = 0; k < _bb_start; k++ )
    _uses[bb->_nodes[k]->_idx] = 1;
  for( uint l = _bb_end; l < bb->_nodes.size(); l++ )
    _uses[bb->_nodes[l]->_idx] = 1;

  // Iterate backwards over the instructions in the block.  Don't count the
  // branch projections at end or the block header instructions.
  for( uint j = _bb_end-1; j >= _bb_start; j-- ) {
    Node *n = bb->_nodes[j];
    if( n->is_Proj() ) continue; // Projections handled another way

    // Account for all uses
    for ( uint k = 0; k < n->len(); k++ ) {
      Node *inp = n->in(k);
      if (!inp) continue;
      assert(inp != n, "no cycles allowed" );
      if( _bbs[inp->_idx] == bb ) { // Block-local use?
        if( inp->is_Proj() )    // Skip through Proj's
          inp = inp->in(0); 
        ++_uses[inp->_idx];     // Count 1 block-local use
      }
    }

    // If this instruction has a 0 use count, then it is available
    if (!_uses[n->_idx]) { 
      _current_latency[n->_idx] = _bundle_cycle_number;
      AddNodeToAvailableList(n);
    }

#ifndef PRODUCT
    if (_cfg->C->trace_opto_output()) {
C2OUT->print("#   uses: %3d: ",_uses[n->_idx]);
      n->dump();
    }
#endif
  }

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("# <- ComputeUseCount\n");
#endif
}

// This routine performs scheduling on each basic block in reverse order,
// using instruction latencies and taking into account function unit
// availability.
void Scheduling::DoScheduling() {
#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("# -> DoScheduling\n");
#endif

  Block *succ_bb = NULL;
  Block *bb;

  // Walk over all the basic blocks in reverse order
  for( int i=_cfg->_num_blocks-1; i >= 0; succ_bb = bb, i-- ) {
    bb = _cfg->_blocks[i];

#ifndef PRODUCT
    if (_cfg->C->trace_opto_output()) {
C2OUT->print("#  Schedule BB#%03d (initial)\n",i);
      for (uint j = 0; j < bb->_nodes.size(); j++)
        bb->_nodes[j]->dump();
    }
#endif

    // On the head node, skip processing
    if( bb == _cfg->_broot )
      continue;

    // Skip empty, connector blocks
    if (bb->is_connector()) 
      continue;

    // If the following block is not the sole successor of
    // this one, then reset the pipeline information
    if (bb->_num_succs != 1 || bb->non_connector_successor(0) != succ_bb) {
#ifndef PRODUCT
      if (_cfg->C->trace_opto_output()) {
C2OUT->print("*** bundle start of next BB, node %d, for %d instructions\n",
                   _next_node->_idx, _bundle_instr_count);
      }
#endif
      step_and_clear();
    }

    // Leave untouched the starting instruction, any Phis, or Top.
    // bb->_nodes[_bb_start] is the first schedulable instruction.
    _bb_end = bb->_nodes.size()-1;
    for( _bb_start=1; _bb_start <= _bb_end; _bb_start++ ) {
      Node *n = bb->_nodes[_bb_start];
      // Things not matched, like Phinodes and ProjNodes don't get scheduled.
      // Also, MachIdealNodes do not get scheduled
      if( !n->is_Mach() ) continue;     // Skip non-machine nodes
      MachNode *mach = n->as_Mach();
      int iop = mach->ideal_Opcode();
      if( iop == Op_Con ) continue;      // Do not schedule Top
      if( iop == Op_Node &&     // Do not schedule PhiNodes, ProjNodes
          mach->pipeline() == MachNode::pipeline_class() &&
          !n->is_SpillCopy() )  // Breakpoints, Prolog, etc
        continue;
      break;                    // Funny loop structure to be sure...
    }
    // Compute last "interesting" instruction in block - last instruction we
    // might schedule.  _bb_end points just after last schedulable inst.  We
    // normally schedule conditional branches (despite them being forced last
    // in the block), because they have delay slots we can fill.  Calls all
    // have their delay slots filled in the template expansions, so we don't
    // bother scheduling them.
    Node *last = bb->_nodes[_bb_end];
    if( last->is_Catch() || 
       (last->is_Mach() && last->as_Mach()->ideal_Opcode() == Op_Halt) ) {
      // There must be a prior call.  Skip it.
      while( !bb->_nodes[--_bb_end]->is_Call() ) {
        assert( bb->_nodes[_bb_end]->is_Proj(), "skipping projections after expected call" );
      }
    } else if( last->is_MachNullCheck() ) {
      // Backup so the last null-checked memory instruction is
      // outside the schedulable range. Skip over the nullcheck,
      // projection, and the memory nodes.
      Node *mem = last->in(1);
      do {
        _bb_end--;
      } while (mem != bb->_nodes[_bb_end]);
    } else {
      // Set _bb_end to point after last schedulable inst.
      _bb_end++;
    }

    assert( _bb_start <= _bb_end, "inverted block ends" );

    // Compute the register antidependencies for the basic block
    ComputeRegisterAntidependencies(bb);
    if (_cfg->C->failing())  return;  // too many D-U pinch points

    // Compute intra-bb latencies for the nodes
    ComputeLocalLatenciesForward(bb);

    // Compute the usage within the block, and set the list of all nodes
    // in the block that have no uses within the block.
    ComputeUseCount(bb);

    // Schedule the remaining instructions in the block
    while ( _available.size() > 0 ) {
      Node *n = ChooseNodeToBundle();
      AddNodeToBundle(n,bb);
    }

    assert( _scheduled.size() == _bb_end - _bb_start, "wrong number of instructions" );
#ifdef ASSERT
    for( uint l = _bb_start; l < _bb_end; l++ ) {
      Node *n = bb->_nodes[l];
      uint m;
      for( m = 0; m < _bb_end-_bb_start; m++ )
        if( _scheduled[m] == n ) 
          break;
      assert( m < _bb_end-_bb_start, "instruction missing in schedule" );
    }
#endif

    // Now copy the instructions (in reverse order) back to the block
    for ( uint k = _bb_start; k < _bb_end; k++ )
      bb->_nodes.map(k, _scheduled[_bb_end-k-1]);

#ifndef PRODUCT
    if (_cfg->C->trace_opto_output()) {
C2OUT->print("#  Schedule BB#%03d (final)\n",i);
      uint current = 0;
      for (uint j = 0; j < bb->_nodes.size(); j++) {
        Node *n = bb->_nodes[j];
        if( valid_bundle_info(n) ) {
          Bundle *bundle = node_bundling(n);
if(bundle->instr_count()>0){
C2OUT->print("*** Bundle: ");
            bundle->dump();
          }
          n->dump();
        }
      }
    }
#endif
#ifdef ASSERT
  verify_good_schedule(bb,"after block local scheduling");
#endif
  }

#ifndef PRODUCT
  if (_cfg->C->trace_opto_output())
C2OUT->print("# <- DoScheduling\n");
#endif
  
  // Record final node-bundling array location
  _regalloc->C->set_node_bundling_base(_node_bundling_base);

} // end DoScheduling

//------------------------------verify_good_schedule---------------------------
// Verify that no live-range used in the block is killed in the block by a
// wrong DEF.  This doesn't verify live-ranges that span blocks.

// Check for edge existence.  Used to avoid adding redundant precedence edges.
static bool edge_from_to( Node *from, Node *to ) {
  for( uint i=0; i<from->len(); i++ )
    if( from->in(i) == to )
      return true;
  return false;
}

#ifdef ASSERT
//------------------------------verify_do_def----------------------------------
void Scheduling::verify_do_def( Node *n, OptoReg::Name def, const char *msg ) {
  // Check for bad kills
  if( OptoReg::is_valid(def) ) { // Ignore stores & control flow
    Node *prior_use = _reg_node[def];
    if( prior_use && !edge_from_to(prior_use,n) ) {
#ifndef PRODUCT
char buf[1024];
_regalloc->print_reg(def,buf);
C2OUT->print("%s = ",buf);
      n->dump();
C2OUT->print_cr("...");
      prior_use->dump();
char*s=buf;
      s = _regalloc->dump_register(prior_use,buf);
      s += sprintf(s," = %s (",prior_use->Name());
for(uint i=0;i<prior_use->len();i++){
Node*u=prior_use->in(i);
        if( u && _regalloc->get_reg(u) != OptoReg::Bad ) {
          s += sprintf(s," %d:",u->_idx);
          s = _regalloc->dump_register(u,s);
        }        
      }
      s += sprintf(s," )");
      C2OUT->print_cr(buf);
#endif
      guarantee(edge_from_to(prior_use,n),"missing from_to edge");
    }
    _reg_node.map(def,NULL); // Kill live USEs
  }
}

//------------------------------verify_good_schedule---------------------------
void Scheduling::verify_good_schedule( Block *b, const char *msg ) {

  // Zap to something reasonable for the verify code
  _reg_node.clear();

  // Walk over the block backwards.  Check to make sure each DEF doesn't
  // kill a live value (other than the one it's supposed to).  Add each
  // USE to the live set.
  for( uint i = b->_nodes.size()-1; i >= _bb_start; i-- ) {
    Node *n = b->_nodes[i];
    int n_op = n->Opcode();
    if( n_op == Op_MachProj && n->ideal_reg() == MachProjNode::fat_proj ) {
      // Fat-proj kills a slew of registers
      RegMask rm = n->out_RegMask();// Make local copy
      while( rm.is_NotEmpty() ) {
        OptoReg::Name kill = rm.find_first_elem();
        rm.Remove(kill);
        verify_do_def( n, kill, msg );
      }
    } else if( n_op != Op_Node ) { // Avoid brand new antidependence nodes
      // Get DEF'd registers the normal way
verify_do_def(n,_regalloc->get_reg(n),msg);
    }

    // Now make all USEs live
    for( uint i=1; i<n->req(); i++ ) {
      Node *def = n->in(i);
      if( def ) {
OptoReg::Name reg_lo=_regalloc->get_reg(def);
        if( OptoReg::is_valid(reg_lo) ) {
#ifdef ASSERT
          guarantee(!_reg_node[reg_lo] || edge_from_to(_reg_node[reg_lo],def), "missing from_to edge" );
#endif
          _reg_node.map(reg_lo,n);
        }
      }
    }

  }

  // Zap to something reasonable for the Antidependence code
  _reg_node.clear();
}
#endif // !ASSERT

// Conditionally add precedence edges.  Avoid putting edges on Projs.
static void add_prec_edge_from_to( Node *from, Node *to ) {
  if( from->is_Proj() ) {       // Put precedence edge on Proj's input
    assert( from->req() == 1 && (from->len() == 1 || from->in(1)==0), "no precedence edges on projections" );
    from = from->in(0);
  }
  if( from != to &&             // No cycles (for things like LD L0,[L0+4] )
      !edge_from_to( from, to ) ) // Avoid duplicate edge
    from->add_prec(to);
}

//------------------------------anti_do_def------------------------------------
void Scheduling::anti_do_def( Block *b, Node *def, OptoReg::Name def_reg, int is_def ) {
  if( !OptoReg::is_valid(def_reg) ) // Ignore stores & control flow
    return;

  Node *pinch = _reg_node[def_reg]; // Get pinch point
  if( !pinch || _bbs[pinch->_idx] != b || // No pinch-point yet?
      is_def ) {    // Check for a true def (not a kill)
    _reg_node.map(def_reg,def); // Record def/kill as the optimistic pinch-point
    return;
  }

  Node *kill = def;             // Rename 'def' to more descriptive 'kill'
  debug_only( def = (Node*)0xdeadbeef; )

  // After some number of kills there _may_ be a later def
  Node *later_def = NULL;

  // Finding a kill requires a real pinch-point.
  // Check for not already having a pinch-point.
  // Pinch points are Op_Node's.
  if( pinch->Opcode() != Op_Node ) { // Or later-def/kill as pinch-point?
    later_def = pinch;            // Must be def/kill as optimistic pinch-point
    if ( _pinch_free_list.size() > 0) {
      pinch = _pinch_free_list.pop();
    } else {
      pinch = new (_cfg->C, 1) Node(1); // Pinch point to-be
    }
    if (pinch->_idx >= _regalloc->node_regs_max_index()) {
_cfg->C->record_failure("too many D-U pinch points",false);
      return;
    }
    _bbs.map(pinch->_idx,b);      // Pretend it's valid in this block (lazy init)
    _reg_node.map(def_reg,pinch); // Record pinch-point
    //_regalloc->set_bad(pinch->_idx); // Already initialized this way.
    if( later_def->outcnt() == 0 || later_def->ideal_reg() == MachProjNode::fat_proj ) { // Distinguish def from kill
      pinch->init_req(0, _cfg->C->top());     // set not NULL for the next call
      add_prec_edge_from_to(later_def,pinch); // Add edge from kill to pinch
      later_def = NULL;           // and no later def
    }
    pinch->set_req(0,later_def);  // Hook later def so we can find it
  } else {                        // Else have valid pinch point
    if( pinch->in(0) )            // If there is a later-def
      later_def = pinch->in(0);   // Get it
  }

  // Add output-dependence edge from later def to kill
  if( later_def )               // If there is some original def
    add_prec_edge_from_to(later_def,kill); // Add edge from def to kill

  // See if current kill is also a use, and so is forced to be the pinch-point.
  if( pinch->Opcode() == Op_Node ) {
    Node *uses = kill->is_Proj() ? kill->in(0) : kill;
    for( uint i=1; i<uses->req(); i++ ) {
if(_regalloc->get_reg(uses->in(i))==def_reg){
        // Yes, found a use/kill pinch-point
        pinch->set_req(0,NULL);  // 
        pinch->replace_by(kill); // Move anti-dep edges up
        pinch = kill;
        _reg_node.map(def_reg,pinch);
        return;
      }
    }    
  }

  // Add edge from kill to pinch-point
  add_prec_edge_from_to(kill,pinch);
}

//------------------------------anti_do_use------------------------------------
void Scheduling::anti_do_use( Block *b, Node *use, OptoReg::Name use_reg ) {
  if( !OptoReg::is_valid(use_reg) ) // Ignore stores & control flow
    return;
  Node *pinch = _reg_node[use_reg]; // Get pinch point
  // Check for no later def_reg/kill in block
  if( pinch && _bbs[pinch->_idx] == b &&
      // Use has to be block-local as well
      _bbs[use->_idx] == b ) {
    if( pinch->Opcode() == Op_Node && // Real pinch-point (not optimistic?)
        pinch->req() == 1 ) {   // pinch not yet in block?
      pinch->del_req(0);        // yank pointer to later-def, also set flag 
      // Insert the pinch-point in the block just after the last use
      b->_nodes.insert(b->find_node(use)+1,pinch);
      _bb_end++;                // Increase size scheduled region in block
    }

    add_prec_edge_from_to(pinch,use);
  }
}

//------------------------------ComputeRegisterAntidependences-----------------
// We insert antidependences between the reads and following write of
// allocated registers to prevent illegal code motion.  Hopefully, the
// number of added references should be fairly small, especially as we
// are only adding references within the current basic block.
void Scheduling::ComputeRegisterAntidependencies(Block *b) {

#ifdef ASSERT
  verify_good_schedule(b,"before block local scheduling");
#endif

  // A valid schedule, for each register independently, is an endless cycle
  // of: a def, then some uses (connected to the def by true dependencies),
  // then some kills (defs with no uses), finally the cycle repeats with a new
  // def.  The uses are allowed to float relative to each other, as are the
  // kills.  No use is allowed to slide past a kill (or def).  This requires
  // antidependencies between all uses of a single def and all kills that
  // follow, up to the next def.  More edges are redundant, because later defs
  // & kills are already serialized with true or antidependencies.  To keep
  // the edge count down, we add a 'pinch point' node if there's more than
  // one use or more than one kill/def.

  // We add dependencies in one bottom-up pass.

  // For each instruction we handle it's DEFs/KILLs, then it's USEs.

  // For each DEF/KILL, we check to see if there's a prior DEF/KILL for this
  // register.  If not, we record the DEF/KILL in _reg_node, the
  // register-to-def mapping.  If there is a prior DEF/KILL, we insert a
  // "pinch point", a new Node that's in the graph but not in the block.
  // We put edges from the prior and current DEF/KILLs to the pinch point.
  // We put the pinch point in _reg_node.  If there's already a pinch point
  // we merely add an edge from the current DEF/KILL to the pinch point.
                               
  // After doing the DEF/KILLs, we handle USEs.  For each used register, we
  // put an edge from the pinch point to the USE.

  // To be expedient, the _reg_node array is pre-allocated for the whole
  // compilation.  _reg_node is lazily initialized; it either contains a NULL,
  // or a valid def/kill/pinch-point, or a leftover node from some prior
  // block.  Leftover node from some prior block is treated like a NULL (no
  // prior def, so no anti-dependence needed).  Valid def is distinguished by
  // it being in the current block.
  bool fat_proj_seen = false;
  uint last_safept = _bb_end-1;
  Node* end_node         = (_bb_end-1 >= _bb_start) ? b->_nodes[last_safept] : NULL;
  Node* last_safept_node = end_node;
  for( uint i = _bb_end-1; i >= _bb_start; i-- ) {
    Node *n = b->_nodes[i];
    int is_def = n->outcnt();   // def if some uses prior to adding precedence edges
    if( n->Opcode() == Op_MachProj && n->ideal_reg() == MachProjNode::fat_proj ) {
      // Fat-proj kills a slew of registers
      // This can add edges to 'n' and obscure whether or not it was a def, 
      // hence the is_def flag.
      fat_proj_seen = true;
      RegMask rm = n->out_RegMask();// Make local copy
      while( rm.is_NotEmpty() ) {
        OptoReg::Name kill = rm.find_first_elem();
        rm.Remove(kill);
        anti_do_def( b, n, kill, is_def );
      }
    } else {
      // Get DEF'd registers the normal way
anti_do_def(b,n,_regalloc->get_reg(n),is_def);
    }

    // Check each register used by this instruction for a following DEF/KILL
    // that must occur afterward and requires an anti-dependence edge.
    for( uint j=0; j<n->req(); j++ ) {
      Node *def = n->in(j);
      if( def ) {
        assert( def->Opcode() != Op_MachProj || def->ideal_reg() != MachProjNode::fat_proj, "" );
anti_do_use(b,n,_regalloc->get_reg(def));
      }
    }
    // Do not allow defs of new derived values to float above GC
    // points unless the base is definitely available at the GC point.

    Node *m = b->_nodes[i];

    // Add precedence edge from following safepoint to use of derived pointer
    if( last_safept_node != end_node && 
        m != last_safept_node) {
      for (uint k = 1; k < m->req(); k++) {
        const Type *t = m->in(k)->bottom_type();
        if( t->isa_oop_ptr() &&
            t->is_ptr()->offset() != 0 ) {
          last_safept_node->add_prec( m );
          break;
        }
      }
    }

    if( n->jvms() ||            // Precedence edge from derived to safept
        (UseSBA && n->base_derived_idx() > 0) ) {
      assert0( n==m );
      // Check if last_safept_node was moved by pinch-point insertion in anti_do_use()
      if( b->_nodes[last_safept] != last_safept_node ) {
        last_safept = b->find_node(last_safept_node);
      }
      for( uint j=last_safept; j > i; j-- ) {
        Node *mach = b->_nodes[j];
        if( mach->is_Mach() && mach->as_Mach()->ideal_Opcode() == Op_AddP )
          mach->add_prec( n );
      }
      if( end_node != last_safept_node && !last_safept_node->is_CFG() && !m->is_CFG() )
        last_safept_node->add_prec(m);
      last_safept = i;
      last_safept_node = m;
    }
  }

  if (fat_proj_seen) {
    // Garbage collect pinch nodes that were not consumed.
    // They are usually created by a fat kill MachProj for a call.
    garbage_collect_pinch_nodes();
  }
}

//------------------------------garbage_collect_pinch_nodes-------------------------------

// Garbage collect pinch nodes for reuse by other blocks.
//
// The block scheduler's insertion of anti-dependence
// edges creates many pinch nodes when the block contains
// 2 or more Calls.  A pinch node is used to prevent a
// combinatorial explosion of edges.  If a set of kills for a
// register is anti-dependent on a set of uses (or defs), rather
// than adding an edge in the graph between each pair of kill
// and use (or def), a pinch is inserted between them:
//
//            use1   use2  use3
//                \   |   /
//                 \  |  /
//                  pinch
//                 /  |  \
//                /   |   \
//            kill1 kill2 kill3
//
// One pinch node is created per register killed when
// the second call is encountered during a backwards pass
// over the block.  Most of these pinch nodes are never
// wired into the graph because the register is never
// used or def'ed in the block.
//
void Scheduling::garbage_collect_pinch_nodes() {
#ifndef PRODUCT
if(_cfg->C->trace_opto_output())C2OUT->print("Reclaimed pinch nodes:");
#endif
    int trace_cnt = 0;
    for (uint k = 0; k < _reg_node.Size(); k++) {
      Node* pinch = _reg_node[k];
      if (pinch != NULL && pinch->Opcode() == Op_Node &&
          // no predecence input edges
          (pinch->req() == pinch->len() || pinch->in(pinch->req()) == NULL) ) {
        cleanup_pinch(pinch);
        _pinch_free_list.push(pinch);
        _reg_node.map(k, NULL);
#ifndef PRODUCT
        if (_cfg->C->trace_opto_output()) {
          trace_cnt++;
          if (trace_cnt > 40) {
C2OUT->print("\n");
            trace_cnt = 0;
          }
C2OUT->print(" %d",pinch->_idx);
        }
#endif
      }
    }
#ifndef PRODUCT
if(_cfg->C->trace_opto_output())C2OUT->print("\n");
#endif
}

// Clean up a pinch node for reuse.
void Scheduling::cleanup_pinch( Node *pinch ) {
  assert (pinch && pinch->Opcode() == Op_Node && pinch->req() == 1, "just checking");

  for (DUIterator_Last imin, i = pinch->last_outs(imin); i >= imin; ) {
    Node* use = pinch->last_out(i);
    uint uses_found = 0;
    for (uint j = use->req(); j < use->len(); j++) {
      if (use->in(j) == pinch) {
        use->rm_prec(j);
        uses_found++;
      }
    }
    assert(uses_found > 0, "must be a precedence edge");
    i -= uses_found;    // we deleted 1 or more copies of this edge
  }
  // May have a later_def entry
  pinch->set_req(0, NULL);
}

//------------------------------print_statistics-------------------------------
#ifndef PRODUCT

void Scheduling::dump_available() const {
C2OUT->print("#Availist  ");
  for (uint i = 0; i < _available.size(); i++)
C2OUT->print(" N%d/l%d",_available[i]->_idx,_current_latency[_available[i]->_idx]);
C2OUT->cr();
}

// Print Scheduling Statistics
void Scheduling::print_statistics() {
  // Print the size added by nops for bundling
C2OUT->print("Nops added %d bytes to total of %d bytes",
    _total_nop_size, _total_method_size);
  if (_total_method_size > 0)
C2OUT->print(", for %.2f%%",
      ((double)_total_nop_size) / ((double) _total_method_size) * 100.0);
C2OUT->print("\n");

  uint total_instructions = 0, total_bundles = 0;

  for (uint i = 1; i <= Pipeline::_max_instrs_per_cycle; i++) {
    uint bundle_count   = _total_instructions_per_bundle[i];
    total_instructions += bundle_count * i;
    total_bundles      += bundle_count;
  }

  if (total_bundles > 0)
C2OUT->print("Average ILP (excluding nops) is %.2f\n",
      ((double)total_instructions) / ((double)total_bundles));
}
#endif
