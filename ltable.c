/*
** $Id: ltable.c,v 1.26 1999/10/14 19:13:31 roberto Exp roberto $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/


/*
** Implementation of tables (aka arrays, objects, or hash tables);
** uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the `original' position that its hash gives
** to it), then the colliding element is in its own main position.
** In other words, there are collisions only when two elements have the
** same main position (i.e. the same hash values for that table size).
** Because of that, the load factor of these tables can be 100% without
** performance penalties.
*/


#include "lauxlib.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "lua.h"


#define gcsize(n)	(1+(n/16))



#define TagDefault LUA_T_ARRAY;



/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
*/
static Node *luaH_mainposition (const Hash *t, const TObject *key) {
  unsigned long h;
  switch (ttype(key)) {
    case LUA_T_NUMBER:
      h = (unsigned long)(long)nvalue(key);
      break;
    case LUA_T_STRING: case LUA_T_USERDATA:
      h = tsvalue(key)->hash;
      break;
    case LUA_T_ARRAY:
      h = (IntPoint)avalue(key);
      break;
    case LUA_T_PROTO:
      h = (IntPoint)tfvalue(key);
      break;
    case LUA_T_CPROTO:
      h = (IntPoint)fvalue(key);
      break;
    case LUA_T_CLOSURE:
      h = (IntPoint)clvalue(key);
      break;
    default:
      lua_error("unexpected type to index table");
      h = 0;  /* to avoid warnings */
  }
  return &t->node[h%(unsigned int)t->size];
}


const TObject *luaH_get (const Hash *t, const TObject *key) {
  Node *n = luaH_mainposition(t, key);
  do {
    if (luaO_equalObj(key, &n->key))
      return &n->val;
    n = n->next;
  } while (n);
  return &luaO_nilobject;
}


int luaH_pos (const Hash *t, const TObject *key) {
  const TObject *v = luaH_get(t, key);
  return (v == &luaO_nilobject) ?  -1 :  /* key not found */
             ((const char *)v - (const char *)(&t->node[0].val))/sizeof(Node);
}



static Node *hashnodecreate (int nhash) {
  Node *v = luaM_newvector(nhash, Node);
  int i;
  for (i=0; i<nhash; i++) {
    ttype(key(&v[i])) = ttype(val(&v[i])) = LUA_T_NIL;
    v[i].next = NULL;
  }
  return v;
}


static void setnodevector (Hash *t, int size) {
  t->node = hashnodecreate(size);
  t->size = size;
  t->firstfree = &t->node[size-1];  /* first free position to be used */
  L->nblocks += gcsize(size);
}


Hash *luaH_new (int size) {
  Hash *t = luaM_new(Hash);
  setnodevector(t, luaO_redimension(size+1));
  t->htag = TagDefault;
  t->next = L->roottable;
  L->roottable = t;
  t->marked = 0;
  return t;
}


void luaH_free (Hash *t) {
  L->nblocks -= gcsize(t->size);
  luaM_free(t->node);
  luaM_free(t);
}


static int newsize (const Hash *t) {
  Node *v = t->node;
  int size = t->size;
  int realuse = 0;
  int i;
  for (i=0; i<size; i++) {
    if (ttype(val(v+i)) != LUA_T_NIL)
      realuse++;
  }
  return luaO_redimension(realuse*2);
}


#ifdef DEBUG
/* check invariant of a table */
static int listfind (const Node *m, const Node *n) {
  do {
    if (m==n) return 1;
    m = m->next;
  } while (m);
  return 0;
}

static int check_invariant (const Hash *t, int filled) {
  Node *n;
  for (n=t->node; n<t->firstfree; n++) {
    TObject *key = &n->key;
    LUA_ASSERT(ttype(key) == LUA_T_NIL || n == luaH_mainposition(t, key),
      "all elements before firstfree are empty or in their main positions");
  }
  if (!filled)
    LUA_ASSERT(ttype(&(n++)->key) == LUA_T_NIL, "firstfree must be empty");
  else
    LUA_ASSERT(n == t->node, "table cannot have empty places");
  for (; n<t->node+t->size; n++) {
    TObject *key = &n->key;
    Node *mp = luaH_mainposition(t, key);
    LUA_ASSERT(ttype(key) != LUA_T_NIL,
      "cannot exist empty elements after firstfree");
    LUA_ASSERT(n == mp || luaH_mainposition(t, &mp->key) == mp,
      "either an element or its colliding element is in its main position");
    LUA_ASSERT(listfind(mp,n), "element is in its main position list");
  }
  return 1;
}
#endif


/*
** the rehash is done in two stages: first, we insert only the elements whose
** main position is free, to avoid needless collisions. In the second stage,
** we insert the other elements.
*/
static void rehash (Hash *t) {
  int oldsize = t->size;
  Node *nold = t->node;
  int i;
  LUA_ASSERT(check_invariant(t, 1), "invalid table");
  L->nblocks -= gcsize(oldsize);
  setnodevector(t, newsize(t));  /* create new array of nodes */
  /* first loop; set only elements that can go in their main positions */
  for (i=0; i<oldsize; i++) {
    Node *old = nold+i;
    if (ttype(&old->val) == LUA_T_NIL)
      old->next = NULL;  /* `remove' it for next loop */
    else {
      Node *mp = luaH_mainposition(t, &old->key);  /* new main position */
      if (ttype(&mp->key) == LUA_T_NIL) {  /* is it empty? */
        mp->key = old->key;  /* put element there */
        mp->val = old->val;
        old->next = NULL;  /* `remove' it for next loop */
      }
      else /* it will be copied in next loop */
        old->next = mp;  /* to be used in next loop */
    }
  }
  /* update `firstfree' */
  while (ttype(&t->firstfree->key) != LUA_T_NIL) t->firstfree--;
  /* second loop; update elements with colision */
  for (i=0; i<oldsize; i++) {
    Node *old = nold+i;
    if (old->next) {  /* wasn't already `removed'? */
      Node *mp = old->next;  /* main position */
      Node *e = t->firstfree;  /* actual position */
      e->key = old->key;  /* put element in the free position */
      e->val = old->val;
      e->next = mp->next;  /* chain actual position in main position's list */
      mp->next = e;
      do {  /* update `firstfree' */
        t->firstfree--;
      } while (ttype(&t->firstfree->key) != LUA_T_NIL);
    }
  }
  LUA_ASSERT(check_invariant(t, 0), "invalid table");
  luaM_free(nold);  /* free old array */
}


/*
** sets a pair key-value in a hash table; first, check whether key is
** already present; if not, check whether key's main position is free;
** if not, check whether colliding node is in its main position or not;
** if it is not, move colliding node to an empty place and put new pair
** in its main position; otherwise (colliding node is in its main position),
** new pair goes to an empty position.
** Tricky point: the only place where an old element is moved is when
** we move the colliding node to an empty place; nevertheless, its old
** value is still in that position until we set the value for the new
** pair; therefore, even when `val' points to an element of this table
** (this happens when we use `luaH_move'), there is no problem.
*/
void luaH_set (Hash *t, const TObject *key, const TObject *val) {
  Node *mp = luaH_mainposition(t, key);
  Node *n = mp;
  do {  /* check whether `key' is somewhere in the chain */
    if (luaO_equalObj(key, &n->key)) {
      n->val = *val;  /* update value */
      return;  /* that's all */
    }
    else n = n->next;
  } while (n);
  /* `key' not found; must insert it */
  if (ttype(&mp->key) != LUA_T_NIL) {  /* main position is not free? */
    Node *othern;  /* main position of colliding node */
    n = t->firstfree;  /* get a free place */
    /* is colliding node out of its main position? (can only happens if
       its position if after "firstfree") */
    if (mp > n && (othern=luaH_mainposition(t, &mp->key)) != mp) {
      /* yes; move colliding node into free position */
      while (othern->next != mp) othern = othern->next;  /* find previous */
      othern->next = n;  /* redo the chain with `n' in place of `mp' */
      *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      mp->next = NULL;  /* now `mp' is free */
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      n->next = mp->next;  /* chain new position */
      mp->next = n;
      mp = n;
    }
  }
  mp->key = *key;
  mp->val = *val;
  for (;;) {  /* check free places */
    if (ttype(&(t->firstfree)->key) == LUA_T_NIL)
      return;  /* OK; table still has a free place */
    else if (t->firstfree == t->node) break;  /* cannot decrement from here */
    else (t->firstfree)--;
  }
  rehash(t);  /* no more free places */
}


void luaH_setint (Hash *t, int key, const TObject *val) {
  TObject index;
  ttype(&index) = LUA_T_NUMBER;
  nvalue(&index) = key;
  luaH_set(t, &index, val);
}


const TObject *luaH_getint (const Hash *t, int key) {
  TObject index;
  ttype(&index) = LUA_T_NUMBER;
  nvalue(&index) = key;
  return luaH_get(t, &index);
}

