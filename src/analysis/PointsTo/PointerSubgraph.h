#ifndef _DG_POINTER_SUBGRAPH_H_
#define _DG_POINTER_SUBGRAPH_H_

#include <cassert>
#include <vector>
#include <cstdarg>
#include <cstring> // for strdup

#include "Pointer.h"
#include "ADT/Queue.h"

namespace dg {
namespace analysis {
namespace pta {

enum PSNodeType {
        // these are nodes that just represent memory allocation sites
        ALLOC = 1,
        DYN_ALLOC,
        LOAD,
        STORE,
        GEP,
        PHI,
        CAST,
        // support for calls via function pointers.
        // The FUNCTION node is the same as ALLOC
        // but having it as separate type has the nice
        // advantage of type checking
        FUNCTION,
        // support for interprocedural analysis,
        // operands are null terminated. It is a noop,
        // just for the user's convenience
        CALL,
        // call via function pointer
        CALL_FUNCPTR,
        // return from the subprocedure (in caller),
        // synonym to PHI
        CALL_RETURN,
        // this is the entry node of a subprocedure
        // and serves just as no op for our convenience,
        // can be optimized away later
        ENTRY,
        // this is the exit node of a subprocedure
        // that returns a value - works as phi node
        RETURN,
        // node that has only one points-to relation
        // that never changes
        CONSTANT,
        // no operation node - this nodes can be used as a branch or join
        // node for convenient PointerSubgraph generation. For example as an
        // unified entry to the function or unified return from the function.
        // These nodes can be optimized away later. No points-to computation
        // is performed on them
        NOOP,
        // copy whole block of memory
        MEMCPY,
        // special nodes
        NULL_ADDR,
        UNKNOWN_MEM,
};

class PSNode
{
    // FIXME: maybe we could use SmallPtrVector or something like that
    std::vector<PSNode *> operands;
    std::vector<PSNode *> successors;
    std::vector<PSNode *> predecessors;

    PSNodeType type;
    Offset offset; // for the case this node is GEP or MEMCPY
    Offset len; // for the case this node is MEMCPY

    // in some cases some nodes are kind of paired - like formal and actual
    // parameters or call and return node. Here the analasis can store
    // such a node - if it needs for generating the PointerSubgraph
    // - it is not used anyhow by the base analysis itself
    // XXX: maybe we cold store this somewhere in a map instead of in every
    // node (if the map is sparse, it would be much more memory efficient)
    PSNode *pairedNode;

    /// some additional information
    // was memory zeroed at initialization or right after allocating?
    bool zeroInitialized;
    // is memory allocated on heap?
    bool is_heap;
    // size of the memory
    size_t size;

#ifdef DEBUG_ENABLED
    const char *name;
#endif

    unsigned int dfsid;
    // data that can an analysis store in node
    // for its own needs
    void *data;

    // data that can user store in the node
    // NOTE: I considered if this way is better than
    // creating subclass of PSNode and have whatever we
    // need in the subclass. Since AFAIK we need just this one pointer
    // at this moment, I decided to do it this way since it
    // is more simple than dynamic_cast... Once we need more
    // than one pointer, we can change this design.
    void *user_data;

public:
    ///
    // Construct a PSNode
    // \param t     type of the node
    // Different types take different arguments:
    //
    // ALLOC:        no argument
    // DYN_ALLOC:    no argument
    // FUNCTION:     no argument
    // NOOP:         no argument
    // ENTRY:        no argument
    // LOAD:         one argument representing pointer to location from where
    //               we're loading the value (another pointer in this case)
    // STORE:        first argument is the value (the pointer to be stored)
    //               in memory pointed by the second argument
    // GEP:          get pointer to memory on given offset (get element pointer)
    //               first argument is pointer to the memory, second is the offset
    //               (as Offset class instance, unknown offset is represented by
    //               UNKNOWN_OFFSET constant)
    // CAST:         cast pointer from one type to other type (like void * to
    //               int *). The pointers are just copied, so we can optimize
    //               away this node later. The argument is just the pointer
    //               (we don't care about types atm.)
    // MEMCPY:       Copy whole block of memory. <from> <to> <offset> <len>
    // FUNCTION:     Object representing the function in memory - so that it
    //               can be pointed to and used as an argument to the Pointer
    // CONSTANT:     node that keeps constant points-to information
    //               the argument is the pointer it points to
    // PHI:          phi node that gathers pointers from different paths in CFG
    //               arguments are null-terminated list of the relevant nodes
    //               from predecessors
    // CALL:         represents call of subprocedure,
    //               arguments are null-terminated list of nodes that can user
    //               use arbitrarily - they are not used by the analysis itself.
    //               The arguments can be used e. g. when mapping call arguments
    //               back to original CFG. Actually, the CALL node is not needed
    //               in most cases (just 'inline' the subprocedure into the PointerSubgraph
    //               when building it)
    // CALL_FUNCPTR: call via function pointer. The argument is the node that
    //               bears the pointers.
    //               FIXME: use more nodes (null-terminated list of pointer nodes)
    // CALL_RETURN:  site where given call returns. Bears the pointers
    //               returned from the subprocedure. Works like PHI
    // RETURN:       represents returning value from a subprocedure,
    //               works as a PHI node - it gathers pointers returned from
    //               the subprocedure
    PSNode(PSNodeType t, ...)
    : type(t), offset(0), pairedNode(nullptr), zeroInitialized(false),
      is_heap(false), size(0),
#ifdef DEBUG_ENABLED
      name(nullptr),
#endif
      dfsid(0), data(nullptr), user_data(nullptr)
    {
        // assing operands
        PSNode *op;
        va_list args;
        va_start(args, t);

        switch(type) {
            case ALLOC:
            case DYN_ALLOC:
            case FUNCTION:
                // these always points-to itself
                // (they points to the node where the memory was allocated)
                addPointsTo(this, 0);
                break;
            case NOOP:
            case ENTRY:
                // no operands
                break;
            case CAST:
            case LOAD:
            case CALL_FUNCPTR:
                operands.push_back(va_arg(args, PSNode *));
                break;
            case STORE:
                operands.push_back(va_arg(args, PSNode *));
                operands.push_back(va_arg(args, PSNode *));
                break;
            case MEMCPY:
                operands.push_back(va_arg(args, PSNode *));
                operands.push_back(va_arg(args, PSNode *));
                offset = va_arg(args, uint64_t);
                len = va_arg(args, uint64_t);
                break;
            case GEP:
                operands.push_back(va_arg(args, PSNode *));
                offset = va_arg(args, uint64_t);
                break;
            case CONSTANT:
                op = va_arg(args, PSNode *);
                offset = va_arg(args, uint64_t);
                pointsTo.insert(Pointer(op, offset));
                break;
            case NULL_ADDR:
                pointsTo.insert(Pointer(this, 0));
                break;
            case pta::UNKNOWN_MEM:
                // UNKNOWN_MEMLOC points to itself
                pointsTo.insert(Pointer(this, UNKNOWN_OFFSET));
                break;
            case CALL_RETURN:
            case PHI:
            case RETURN:
            case CALL:
                op = va_arg(args, PSNode *);
                // the operands are null terminated
                while (op) {
                    operands.push_back(op);
                    op = va_arg(args, PSNode *);
                }
                break;
            default:
                assert(0 && "Unknown type");
        }

        va_end(args);
    }

    ~PSNode() { delete name; }

    // getters & setters for analysis's data in the node
    template <typename T>
    T* getData() { return static_cast<T *>(data); }
    template <typename T>
    const T* getData() const { return static_cast<T *>(data); }

    template <typename T>
    void *setData(T *newdata)
    {
        void *old = data;
        data = static_cast<void *>(newdata);
        return old;
    }

    // getters & setters for user's data in the node
    template <typename T>
    T* getUserData() { return static_cast<T *>(user_data); }
    template <typename T>
    const T* getUserData() const { return static_cast<T *>(user_data); }

    template <typename T>
    void *setUserData(T *newdata)
    {
        void *old = user_data;
        user_data = static_cast<void *>(newdata);
        return old;
    }

    PSNodeType getType() const { return type; }
    const char *getName() const { return name; }
    void setName(const char *n)
    {
        delete name;
        name = strdup(n);
    }

    PSNode *getPairedNode() const { return pairedNode; }
    void setPairedNode(PSNode *n) { pairedNode = n; }

    PSNode *getOperand(int idx) const
    {
        assert(idx >= 0 && (size_t) idx < operands.size()
               && "Operand index out of range");

        return operands[idx];
    }

    size_t addOperand(PSNode *n)
    {
        operands.push_back(n);
        return operands.size();
    }

    void setZeroInitialized() { zeroInitialized = true; }
    bool isZeroInitialized() const { return zeroInitialized; }

    void setIsHeap() { is_heap = true; }
    bool isHeap() const { return is_heap; }

    void setSize(size_t s) { size = s; }
    size_t getSize() const { return size; }

    bool isNull() const { return type == NULL_ADDR; }
    bool isUnknownMemory() const { return type == UNKNOWN_MEM; }

    void addSuccessor(PSNode *succ)
    {
        successors.push_back(succ);
        succ->predecessors.push_back(this);
    }

    void replaceSingleSuccessor(PSNode *succ)
    {
        assert(successors.size() == 1);
        PSNode *old = successors[0];

        // replace the successor
        successors.clear();
        addSuccessor(succ);

        // we need to remove this node from
        // successor's predecessors
        std::vector<PSNode *> tmp;
        tmp.reserve(old->predecessorsNum() - 1);
        for (PSNode *p : old->predecessors)
            tmp.push_back(p);

        old->predecessors.swap(tmp);
    }

    // return const only, so that we cannot change them
    // other way then addSuccessor()
    const std::vector<PSNode *>& getSuccessors() const { return successors; }
    const std::vector<PSNode *>& getPredecessors() const { return predecessors; }

    // get successor when we know there's only one of them
    PSNode *getSingleSuccessor() const
    {
        assert(successors.size() == 1);
        return successors.front();
    }

    // get predecessor when we know there's only one of them
    PSNode *getSinglePredecessor() const
    {
        assert(predecessors.size() == 1);
        return predecessors.front();
    }

    // insert this node in PointerSubgraph after n
    // this node must not be in any PointerSubgraph
    void insertAfter(PSNode *n)
    {
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over successors
        successors.swap(n->successors);

        // make this node the successor of n
        n->addSuccessor(this);

        // replace the reference to n in successors
        for (PSNode *succ : successors) {
            for (unsigned i = 0; i < succ->predecessorsNum(); ++i) {
                if (succ->predecessors[i] == n)
                    succ->predecessors[i] = this;
            }
        }
    }

    // insert this node in PointerSubgraph before n
    // this node must not be in any PointerSubgraph
    void insertBefore(PSNode *n)
    {
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over predecessors
        predecessors.swap(n->predecessors);

        // 'n' is a successors of this node
        addSuccessor(n);

        // replace the reference to n in predecessors
        for (PSNode *pred : predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->successors[i] == n)
                    pred->successors[i] = this;
            }
        }
    }

    // insert a sequence before this node in PointerSubgraph
    void insertSequenceBefore(std::pair<PSNode *, PSNode *>& seq)
    {
        // the sequence must not be inserted in any PointerSubgraph
        assert(seq.first->predecessorsNum() == 0);
        assert(seq.second->successorsNum() == 0);

        // first node of the sequence takes over predecessors
        // this also clears 'this->predecessors' since seq.first
        // has no predecessors
        predecessors.swap(seq.first->predecessors);

        // replace the reference to 'this' in predecessors
        for (PSNode *pred : seq.first->predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->successors[i] == this)
                    pred->successors[i] = seq.first;
            }
        }

        // this node is successors of the last node in sequence
        seq.second->addSuccessor(this);
    }

    size_t predecessorsNum() const { return predecessors.size(); }
    size_t successorsNum() const { return successors.size(); }

    // make this public, that's basically the only
    // reason the PointerSubgraph node exists, so don't hide it
    PointsToSetT pointsTo;

    // convenient helper
    bool addPointsTo(PSNode *n, Offset o)
    {
        // do not add concrete offsets when we have the UNKNOWN_OFFSET
        // - unknown offset stands for any offset
        if (pointsTo.count(Pointer(n, UNKNOWN_OFFSET)))
            return false;

        if (o.isUnknown())
            return addPointsToUnknownOffset(n);
        else
            return pointsTo.insert(Pointer(n, o)).second;
    }

    bool addPointsTo(const Pointer& ptr)
    {
        return addPointsTo(ptr.target, ptr.offset);
    }

    bool addPointsTo(const std::set<Pointer>& ptrs)
    {
        bool changed = false;
        for (const Pointer& ptr: ptrs)
            changed |= addPointsTo(ptr);

        return changed;
    }

    bool doesPointsTo(const Pointer& p)
    {
        return pointsTo.count(p) == 1;
    }

    bool doesPointsTo(PSNode *n, Offset o = 0)
    {
        return doesPointsTo(Pointer(n, o));
    }

    bool addPointsToUnknownOffset(PSNode *target);

    friend class PointerSubgraph;
};

// special PointerSubgraph nodes
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;

class PointerSubgraph
{
    unsigned int dfsnum;

    // root of the pointer state subgraph
    PSNode *root;

protected:
    // queue used to reach the fixpoint
    ADT::QueueFIFO<PSNode *> queue;

    // protected constructor for child classes
    PointerSubgraph() : dfsnum(0), root(nullptr) {}

public:
    bool processNode(PSNode *);
    PointerSubgraph(PSNode *r) : dfsnum(0), root(r)
    {
        assert(root && "Cannot create PointerSubgraph with null root");
    }

    virtual ~PointerSubgraph() {}

    // takes a PSNode 'where' and 'what' and reference to a vector
    // and fills into the vector the objects that are relevant
    // for the PSNode 'what' (valid memory states for of this PSNode)
    // on location 'where' in PointerSubgraph
    virtual void getMemoryObjects(PSNode *where, PSNode *what,
                                  std::vector<MemoryObject *>& objects) = 0;

    /*
    virtual bool addEdge(MemoryObject *from, MemoryObject *to,
                         Offset off1 = 0, Offset off2 = 0)
    {
        return false;
    }
    */

    void getNodes(std::set<PSNode *>& cont,
                  PSNode *n = nullptr)
    {
        // default behaviour is to enqueue all pending nodes
        ++dfsnum;
        ADT::QueueFIFO<PSNode *> fifo;

        if (!n) {
            fifo.push(root);
            n = root;
        }

        for (PSNode *succ : n->successors) {
            succ->dfsid = dfsnum;
            fifo.push(succ);
        }

        while (!fifo.empty()) {
            PSNode *cur = fifo.pop();
            bool ret = cont.insert(cur).second;
            assert(ret && "BUG: Tried to insert something twice");

            for (PSNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifo.push(succ);
                }
            }
        }
    }

    // get nodes in BFS order and store them into
    // the container
    template <typename ContT>
    void getNodes(ContT& cont,
                  PSNode *start_node = nullptr,
                  std::set<PSNode *> *start_set = nullptr)
    {
        assert(root && "Do not have root");
        assert(!(start_set && start_node)
               && "Need either starting set or starting node, not both");

        ++dfsnum;
        ADT::QueueFIFO<PSNode *> fifo;

        if (start_set) {
            // FIXME: get rid of the loop,
            // make it via iterator range
            for (PSNode *s : *start_set)
                fifo.push(s);
        } else {
            if (!start_node)
                start_node = root;

            fifo.push(start_node);
        }

        root->dfsid = dfsnum;

        while (!fifo.empty()) {
            PSNode *cur = fifo.pop();
            cont.push(cur);

            for (PSNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifo.push(succ);
                }
            }
        }
    }

    virtual void enqueue(PSNode *n)
    {
        // default behaviour is to queue all reachable nodes
        getNodes(queue, n);
    }

    /* hooks for analysis - optional */
    virtual void beforeProcessed(PSNode *n)
    {
        (void) n;
    }

    virtual void afterProcessed(PSNode *n)
    {
        (void) n;
    }

    PSNode *getRoot() const { return root; }
    void setRoot(PSNode *r) { root = r; }

    size_t pendingInQueue() const { return queue.size(); }

    void run()
    {
        assert(root && "Do not have root");
        // initialize the queue
        // FIXME let user do that
        queue.push(root);
        getNodes(queue);

        while (!queue.empty()) {
            PSNode *cur = queue.pop();
            beforeProcessed(cur);

            if (processNode(cur))
                enqueue(cur);

            afterProcessed(cur);
        }

        // FIXME: There's a bug in flow-sensitive that it does
        // not reach fixpoint in the loop above, because it reads
        // from values that has not been processed yet (thus it has
        // empty points-to set) - nothing is changed, so it seems
        // that we reached fixpoint, but we didn't and we fail
        // the assert below. This is temporary workaround -
        // just make another iteration. Proper fix would be to
        // fix queuing the nodes, but that will be more difficult
        //queue.push(root);
        //getNodes(queue);

        while (!queue.empty()) {
            PSNode *cur = queue.pop();
            beforeProcessed(cur);

            if (processNode(cur))
                enqueue(cur);

            afterProcessed(cur);
        }

#ifdef DEBUG_ENABLED
        // NOTE: This works as assertion,
        // we'd like to be sure that we have reached the fixpoint,
        // so we'll do one more iteration and check it

        queue.push(root);
        getNodes(queue);

        bool changed = false;
        while (!queue.empty()) {
            PSNode *cur = queue.pop();

            beforeProcessed(cur);

            changed = processNode(cur);
        //    assert(!changed && "BUG: Did not reach fixpoint");

            afterProcessed(cur);
        }
#endif // DEBUG_ENABLED
    }

    // generic error
    // @msg - message for the user
    // FIXME: maybe create some enum that will represent the error
    virtual bool error(PSNode *at, const char *msg)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        (void) at;
        (void) msg;
        return false;
    }

    // handle specific situation (error) in the analysis
    // @return whether the function changed the some points-to set
    //  (e. g. added pointer to unknown memory)
    virtual bool errorEmptyPointsTo(PSNode *from, PSNode *to)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        (void) from;
        (void) to;
        return false;
    }

    // adjust the PointerSubgraph on function pointer call
    // @ where is the callsite
    // @ what is the function that is being called
    virtual bool functionPointerCall(PSNode *where, PSNode *what)
    {
        (void) where;
        (void) what;
        return false;
    }

private:
    bool processLoad(PSNode *node);
    bool processMemcpy(PSNode *node);
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
