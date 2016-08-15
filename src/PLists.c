/* Portable list handling code. */
#include "PLists.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

INLINE List *NewPList(void)
{
   List *l;

   if((l = (List *) calloc(1,sizeof(List))))
   {
      l->lh_Tail = NULL;
      l->lh_Head = (Node *) &l->lh_Tail;
      l->lh_TailPred = (Node *) l;
   }

   return(l);
}

INLINE Node *NewNode(void)
{
   return (Node *) calloc(1,sizeof(Node));
}

INLINE Node *NewDataNode(unsigned long datasize)
{
   register Node *n;
   register void *data;

   n = (Node *) calloc(1,sizeof(Node));
   if(n)
   {
      data = (void *) calloc(1,datasize);
      if(data)
         SetData(n,data);
      else
      {
         FreeNode(n);
         n = NULL;
      }
   }

   return(n);
}

INLINE void FreeNode(Node *n)
{
   if(n)
   {
#ifndef NOFREEDATA
      if(NodeData(n))
         free(NodeData(n));
#endif
      free(n);
   }
}

INLINE void MapOnNodes(List *y,void (*NodeFunc)(Node *Data))
{
   Node *n,*nxt;

   if(!IsPListEmpty(y))
   {
      for(n = (Node *) FirstNode(y); !AtListEnd(n); n = nxt)
      {
         /* Get the next node, so if its removed we don't access bad mem. */
         nxt = NxtNode(n);
         NodeFunc(n);
      }
   }
}

INLINE void FreeList(List *y)
{
   if(y)
   {
      MapOnNodes(y,RemFreeNode);
      free(y);
   }
}

INLINE void RemFreeNode(Node *n)
{
   Remove(n);
   FreeNode(n);
}

INLINE unsigned long NodeCount(List *l)
{
   register Node *n;
   register unsigned long count = 0;

   if(!IsPListEmpty(l))
   {
      for(n = FirstNode(l); !AtListEnd(n) ;count++,n = NxtNode(n))
         ;
   }

   return count;
}

INLINE void MapOnNodeData(List *y,void (*DataFunc)(void *Data))
{
   register Node *n;

   if(!IsPListEmpty(y))
   {
      for(n = (Node *) FirstNode(y); !AtListEnd(n); n = NxtNode(n))
         DataFunc(NodeData(n));
   }
}

INLINE void MapOnNodeDataC(List *y,void *c,void (*DataFunc)(void *,void *))
{
   register Node *n;

   if(!IsPListEmpty(y))
   {
      for(n = (Node *) FirstNode(y); !AtListEnd(n); n = NxtNode(n))
         DataFunc(c,NodeData(n));
   }
}

/* All Code Below Is Amiga Emulation Code */
INLINE void Insert(List *y,Node *n,Node *p)
{
   if(n)
   {
      n->ln_Succ = NxtNode(p);
      n->ln_Pred = p;
      n->ln_Succ->ln_Pred = n;
      n->ln_Pred->ln_Succ = n;
   }
}

INLINE void AddHead(List *y,Node *n)
{
   Node *h;
   if(n && y)
   {
      h = (Node *) FirstNode(y);
      n->ln_Succ = h;
      n->ln_Pred = (Node *) y;
      h->ln_Pred = n;
      y->lh_Head = n;
   }
}

INLINE void AddTail(List *y,Node *n)
{
   Node *t;

   if(n && y)
   {
      t = y->lh_TailPred;
      n->ln_Succ = (Node *) &y->lh_Tail;
      n->ln_Pred = t;
      y->lh_TailPred = n;
      t->ln_Succ = n;
   }
}

INLINE void Remove(Node *n)
{
   Node *nxt,*prv;

   nxt = NxtNode(n);
   prv = PrvNode(n);

   if(nxt && prv)
   {
      nxt->ln_Pred = prv;
      prv->ln_Succ = nxt;
   }
   n->ln_Pred = NULL;
   n->ln_Succ = NULL;
}

INLINE void RemHead(List *y)
{
   Node *n,*h;

   if(!IsPListEmpty(y))
   {
      n = FirstNode(y);
      h = NxtNode(n);
      h->ln_Pred = (Node *) y;
      y->lh_Head = h;
   }
}

INLINE void RemTail(List *y)
{
   Node *p;

   if(!IsPListEmpty(y))
   {
      p = PrvNode(y->lh_TailPred);
      p->ln_Succ = (Node *) &y->lh_Tail;
      y->lh_TailPred = p;
   }
}
