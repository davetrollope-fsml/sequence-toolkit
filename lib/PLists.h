/** @file PLists.h
 * Portable List structures and definitions.
 */
#ifndef NULL
#define NULL 0L
#endif

#ifndef PLISTS
#define PLISTS

struct ListNode;
typedef struct ListNode Node;
struct ListNode
{
   Node *ln_Succ;
   Node *ln_Pred;
   void *ln_Data;
#ifdef SUBTREES
   unsigned long SubLevel; /* Sub Level */
#endif
};

struct PrivList
{
   Node *lh_Head;
   Node *lh_Tail;         /* Always NULL */
   Node *lh_TailPred;
#ifdef LISTDATA
   void *ListData;
#endif
};
typedef struct PrivList List;
#ifdef DEFENSIVE
#include <assert.h>
int nassert(char *x)
{
   _assert(x,__FILE__,__LINE__);
   return NULL;
}
#define FirstNode(l) ((l) ? ((List *) (l))->lh_Head : nassert("DCF FirstNode"))
#define LastNode(l) ((l) ? ((List *) (l))->lh_TailPred : nassert("DCF LastNode"))
#define NxtNode(n) ((n) ? ((Node *) (n))->ln_Succ : nassert("DCF NxtNode"))
#define PrvNode(n) ((n) ? ((Node *) (n))->ln_Pred : nassert("DCF PrvNode"))
#define SetData(n, d) (n ? ((Node *) (n))->ln_Data = (void *) (d) : nassert("DCF SetData"))
#define NodeData(n) (n ? ((Node *) (n))->ln_Data : nassert("DCF NodeData"))


#ifdef SUBTREES
#define SetSubLevel(n,level) (n ? ((Node *) (n))->SubLevel = (unsigned long) (level) : nassert("DCF SetSubLevel"))
#define SubLevel(n) ((Node *) (n ? (n))->SubLevel : nassert("DCF SubLevel"))
#define IncLevel(n) ((Node *) (n ? (n))->SubLevel++ : nassert("DCF SubLevel"))
#define DecLevel(n) ((Node *) (n ? (n))->SubLevel-- : nassert("DCF DecLevel"))
#else
#define SetSubLevel(n,level) nassert("DCF SetSubLevel called for a non-tree configuration")
#define SubLevel(n) nassert("DCF SubLevel called for a non-tree configuration")
#define IncLevel(n) nassert("DCF IncLevel called for a non-tree configuration")
#define DecLevel(n) nassert("DCF DecubLevel called for a non-tree configuration")
#endif
#else
#define FirstNode(l) (((List *) (l))->lh_Head)
#define LastNode(l) (((List *) (l))->lh_TailPred)
#define NxtNode(n) (((Node *) (n))->ln_Succ)
#define PrvNode(n) (((Node *) (n))->ln_Pred)
#define SetData(n, d) ((Node *) (n))->ln_Data = (void *) (d)
#define NodeData(n) ((Node *) (n))->ln_Data
#define ListFromLastNode(n) ((List *)(((char *) n->ln_Succ) - ((char *) &((List *)0)->lh_Tail)))
#ifdef SUBTREES
#define SetSubLevel(n,level) ((Node *) (n))->SubLevel = (unsigned long) (level)
#define SubLevel(n) ((Node *) (n))->SubLevel
#define IncLevel(n) ((Node *) (n))->SubLevel++
#define DecLevel(n) ((Node *) (n))->SubLevel--
#else
#define SetSubLevel(n,level) ;
#define SubLevel(n) ;
#define IncLevel(n) ;
#define DecLevel(n) ;
#endif
#endif

#define IsTail(n) (!NxtNode(NxtNode(n)))
#define IsHead(n) (!PrvNode(PrvNode(n)))
#define IsPListEmpty(l) (LastNode(l) == (Node *) (l) )
#define AtListEnd(n) (!NxtNode(n))
#define IsTailPred(n) (!NxtNode(NxtNode(n)))
#define IsLastNode(n) IsTailPred(n)
#define IsFirstNode(n) IsHead(n)
#define IsLinked(n) ((n->ln_Succ != NULL) || (n->ln_Pred != NULL))

#ifdef LISTDATA
   #define SetLData(l, d) (((List *) l)->ListData = (void *) d)
   #define GetLData(l) (((List *) l)->ListData)
#endif

#ifndef NOPROTOS
   List *NewPList(void);
   Node *NewNode(void);
   Node *NewDataNode(unsigned long datasize);
   void FreeNode(Node *n);
   void FreeList(List *y);
   void RemFreeNode(Node *n);
   unsigned long NodeCount(List *l);
   void MapOnNodes(List *y,void (*NodeFunc)(Node *Data));
   void MapOnNodeData(List *y,void (*DataFunc)(void *Data));
   void MapOnNodeDataC(List *y,void *obj,void (*DataFunc)(void *obj,void *Data));
   void Insert(List *y,Node *n,Node *p);
   void AddHead(List *y,Node *n);
   void AddTail(List *y,Node *n);
   void Remove(Node *n);
   void RemHead(List *y);
   void RemTail(List *y);
#else
   List *NewPList();
   Node *NewNode();
   Node *NewDataNode();
   void FreeNode();
   void FreeList();
   void RemFreeNode();
   unsigned long NodeCount();
   void MapOnNodes();
   void MapOnNodeData();
   void MapOnNodeDataC();
   void Insert();
   void AddHead();
   void AddTail();
   void Remove();
   void RemHead();
   void RemTail();
#endif

#ifdef INLINEPLIST
#define INLINE inline
#include "PLists.c"
#else
#define INLINE
#endif

#endif
