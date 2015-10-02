/**
  This file belong to the KMPlayer project, a movie player plugin for Konqueror
  Copyright (C) 2009  Koos Vriezen <koos.vriezen@gmail.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
**/

#ifdef KMPLAYER_EXPR_DEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <qurl.h>
#include "expression.h"

#include <QRegExp>

using namespace KMPlayer;

QString NodeValue::value () const {
    if (attr)
        return attr->value ();
    if (node)
        return node->nodeValue ();
    return string;
}

namespace {

struct EvalState {
    EvalState (EvalState *p, const QString &root_tag=QString())
     : def_root_tag (root_tag), root (NULL),
       process_list (NULL), parent (p),
       sequence (1), ref_count (0) {}

    void addRef () { ++ref_count; }
    void removeRef () { if (--ref_count == 0) delete this; }

    QString def_root_tag;
    NodeValue root;
    Sequence *process_list;
    EvalState *parent;
    int sequence;
    int ref_count;
};

struct AST : public Expression {
    enum Type {
        TUnknown, TInteger, TBool, TFloat, TString
    };

    AST (EvalState *ev);
    virtual ~AST ();

    virtual bool toBool () const;
    virtual int toInt () const;
    virtual float toFloat () const;
    virtual QString toString () const;
    virtual Sequence *toSequence () const; // uncachable
    virtual Type type () const;
    virtual void setRoot (Node *root);
    void setRoot (const NodeValue &value);
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif

    mutable int sequence;
    mutable EvalState *eval_state;
    AST *first_child;
    AST *next_sibling;
};

struct BoolBase : public AST {
    BoolBase (EvalState *ev) : AST (ev), b (false) {}

    virtual QString toString () const;
    virtual Type type () const;

    mutable bool b;
};

struct NumberBase : public AST {
    NumberBase (EvalState *ev) : AST (ev) {}

    virtual bool toBool () const;
};

struct IntegerBase : public NumberBase {
    IntegerBase (EvalState *ev) : NumberBase (ev), i (0) {}

    virtual float toFloat () const;
    virtual Type type () const;

    mutable int i;
};

struct Integer : public IntegerBase {
    Integer (EvalState *ev, int i_) : IntegerBase (ev) {
        i = i_;
    }

    virtual int toInt () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Float : public AST {
    Float (EvalState *ev, float f_) : AST (ev), f (f_) {}

    bool toBool () const { return false; }
    int toInt () const { return (int) f; }
    float toFloat () const { return f; }
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const {
        fprintf (stderr, "Float %f", f);
        AST::dump();
    }
#endif

    float f;
};

struct StringBase : public AST {
    StringBase (EvalState *ev) : AST (ev) {}
    StringBase (EvalState *ev, const QString& s)
     : AST (ev), string(s) {}

    virtual bool toBool () const;
    virtual int toInt () const;
    virtual float toFloat () const;
    virtual Type type () const;

    mutable QString string;
};

struct SequenceBase : public StringBase {
    SequenceBase (EvalState *ev) : StringBase (ev) {}
    SequenceBase (EvalState *ev, const QString& s)
        : StringBase (ev, s) {}

    virtual bool toBool () const;
    virtual QString toString () const;
    virtual Type type () const;
};

struct Step : public SequenceBase {
    Step (EvalState *ev, bool context=false)
     : SequenceBase (ev),
       any_node (false), context_node (context),
       is_attr (false), recursive (false) {}
    Step (EvalState *ev, const QString &s, bool isattr, bool rec)
     : SequenceBase (ev, s),
       any_node (string == "*"), context_node (false),
       is_attr (isattr), recursive (rec) {}

    Sequence *toSequence () const;
    bool matches (Node *n) const;
    bool matches (Attribute *a) const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const {
        fprintf (stderr, "Step %c%s",
                is_attr ? '@' : ' ',
                context_node ? "." : string.toAscii ().constData ());
        AST::dump();
    }
#endif

    bool any_node;
    bool context_node;
    bool is_attr;
    bool recursive;
};

struct Path : public SequenceBase {
    Path (EvalState *ev, AST *steps, bool context)
        : SequenceBase (ev), start_contextual (context) {
        first_child = steps;
    }

    virtual Sequence *toSequence () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const {
        fprintf (stderr, "Path ");
        AST::dump();
    }
#endif
    bool start_contextual;
};

struct PredicateFilter : public SequenceBase {
    PredicateFilter (EvalState *ev, AST *children) : SequenceBase (ev) {
        first_child = children;
    }

    virtual Sequence *toSequence () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const {
        fprintf (stderr, "Predicate ");
        first_child->dump();
        fprintf (stderr, "[");
        for (AST *n = first_child->next_sibling; n; n = n->next_sibling)
            n->dump();
        fprintf (stderr, "]");
    }
#endif
};

struct StringLiteral : public StringBase {
    StringLiteral (EvalState *ev, const QString& s)
     : StringBase (ev, s) {}

    virtual QString toString () const;
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const {
        fprintf (stderr, "StringLiteral %s", string.toAscii ().constData ());
        AST::dump();
    }
#endif
};

struct Contains : public BoolBase {
    Contains (EvalState *ev) : BoolBase (ev) {}

    virtual bool toBool () const;
};

struct Not : public BoolBase {
    Not (EvalState *ev) : BoolBase (ev) {}

    virtual bool toBool () const;
};

struct StartsWith: public BoolBase {
    StartsWith (EvalState *ev) : BoolBase (ev) {}

    virtual bool toBool () const;
};

struct Count : public IntegerBase {
    Count (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct HoursFromTime : public IntegerBase {
    HoursFromTime (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct MinutesFromTime : public IntegerBase {
    MinutesFromTime (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct SecondsFromTime : public IntegerBase {
    SecondsFromTime (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct Last : public IntegerBase {
    Last (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct Number : public IntegerBase {
    Number (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct Position : public IntegerBase {
    Position (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct StringLength : public IntegerBase {
    StringLength (EvalState *ev) : IntegerBase (ev) {}

    virtual int toInt () const;
};

struct Concat : public StringBase {
    Concat (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct StringJoin : public StringBase {
    StringJoin (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct SubstringAfter : public StringBase {
    SubstringAfter (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct SubstringBefore : public StringBase {
    SubstringBefore (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct CurrentTime : public StringBase {
    CurrentTime (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct CurrentDate : public StringBase {
    CurrentDate (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct EscapeUri : public StringBase {
    EscapeUri (EvalState *ev) : StringBase (ev) {}

    virtual QString toString () const;
};

struct Sort : public SequenceBase {
    Sort (EvalState *ev) : SequenceBase (ev) {}

    virtual Sequence *toSequence () const;
};

struct SubSequence : public SequenceBase {
    SubSequence (EvalState *ev) : SequenceBase (ev) {}

    virtual Sequence *toSequence () const;
};

struct Tokenize : public SequenceBase {
    Tokenize (EvalState *ev) : SequenceBase (ev) {}

    virtual Sequence *toSequence () const;
};

struct Multiply : public NumberBase {
    Multiply (EvalState *ev, AST *children) : NumberBase (ev) {
        first_child = children;
    }

    virtual int toInt () const;
    virtual float toFloat () const;
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Divide : public NumberBase {
    Divide (EvalState *ev, AST *children) : NumberBase (ev) {
        first_child = children;
    }

    virtual int toInt () const;
    virtual float toFloat () const;
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Modulus : public NumberBase {
    Modulus (EvalState *ev, AST *children) : NumberBase (ev) {
        first_child = children;
    }

    virtual int toInt () const;
    virtual float toFloat () const;
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Plus : public NumberBase {
    Plus (EvalState *ev, AST *children) : NumberBase (ev) {
        first_child = children;
    }

    virtual int toInt () const;
    virtual float toFloat () const;
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Minus : public NumberBase {
    Minus (EvalState *ev, AST *children) : NumberBase (ev) {
        first_child = children;
    }

    virtual int toInt () const;
    virtual float toFloat () const;
    virtual Type type () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Join : public SequenceBase {
    Join (EvalState *ev, AST *children) : SequenceBase (ev) {
        first_child = children;
    }

    virtual Sequence *toSequence () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif
};

struct Comparison : public BoolBase {
    enum CompType {
        lt = 1, lteq, eq, noteq, gt, gteq, land, lor
    };

    Comparison (EvalState *ev, CompType ct, AST *children)
     : BoolBase (ev), comp_type (ct) {
        first_child = children;
    }

    virtual bool toBool () const;
#ifdef KMPLAYER_EXPR_DEBUG
    virtual void dump () const;
#endif

    CompType comp_type;
};

}

AST::AST (EvalState *ev)
 : sequence (0), eval_state (ev), first_child (NULL), next_sibling (NULL) {
    ev->addRef ();
}

AST::~AST () {
    while (first_child) {
        AST *tmp = first_child;
        first_child = first_child->next_sibling;
        delete tmp;
    }
    eval_state->removeRef ();
}

bool AST::toBool () const {
    return toInt ();
}

int AST::toInt () const {
    return 0;
}

float AST::toFloat () const {
    return 0.0;
}

QString AST::toString () const {
    switch (type ()) {
    case TBool:
        return toBool () ? "true" : "false";
    case TInteger:
        return QString::number (toInt ());
    case TFloat:
        return QString::number (toFloat ());
    default:
        return QString ();
    }
}

Sequence *AST::toSequence () const {
    return new Sequence;
}

AST::Type AST::type () const {
    return TUnknown;
}

void AST::setRoot (Node *root) {
    setRoot (NodeValue (root));
}

void AST::setRoot (const NodeValue& value) {
    eval_state->root = value;
    eval_state->sequence++;
}

#ifdef KMPLAYER_EXPR_DEBUG
void AST::dump () const {
    if (first_child) {
        fprintf (stderr, "[ ");
        for (AST *child = first_child; child; child = child->next_sibling) {
            if (child != first_child)
                fprintf (stderr, ", ");
            child->dump();
        }
        fprintf (stderr, " ]");
    }
}
#endif

static void appendASTChild (AST *p, AST *c) {
    if (!p->first_child)
        p->first_child = c;
    else
        for (AST *chld = p->first_child; chld; chld = chld->next_sibling)
            if (!chld->next_sibling) {
                chld->next_sibling = c;
                break;
            }
}

static AST *releaseASTChildren (AST *p) {
    AST *child = p->first_child;
    p->first_child = NULL;
    return child;
}

static AST *releaseLastASTChild (AST *p) {
    AST **chldptr = &p->first_child;
    while ((*chldptr)->next_sibling)
        chldptr = &(*chldptr)->next_sibling;
    AST *last = *chldptr;
    *chldptr = NULL;
    return last;
}

QString BoolBase::toString () const {
    return toBool () ? "true" : "false";
}

AST::Type BoolBase::type () const {
    return TBool;
}

bool NumberBase::toBool () const {
    int ii = toInt ();
    if (eval_state->parent) {
        Sequence *lst = eval_state->parent->process_list;
        if (lst) {
            int count = 0;
            for (NodeValueItem *n = lst->first (); n; n = n->nextSibling ())
                if (ii == ++count)
                    return eval_state->root.node
                        ? eval_state->root.node == n->data.node &&
                          eval_state->root.attr == n->data.attr
                        : eval_state->root.value () == n->data.value ();
        }
        return false;
    }
    return ii;
}

float IntegerBase::toFloat () const {
    return toInt ();
}

AST::Type IntegerBase::type () const {
    return TInteger;
}

int Integer::toInt () const {
    return i;
}

#ifdef KMPLAYER_EXPR_DEBUG
void Integer::dump () const {
    fprintf (stderr, "Integer %d", i);
    AST::dump();
}
#endif

AST::Type Float::type () const {
    return TFloat;
}

bool StringBase::toBool () const {
    QString s = toString ();
    if (s.toLower () == "true")
        return true;
    if (s.toLower () == "false")
        return false;
    return s.toInt ();
}

int StringBase::toInt () const {
    return toString ().toInt ();
}

float StringBase::toFloat () const {
    return toString ().toFloat ();
}

AST::Type StringBase::type () const {
    return TString;
}

bool Step::matches (Node *n) const {
    return any_node || string == n->nodeName ();
}

bool Step::matches (Attribute *a) const {
    return any_node || string == a->name ();
}

bool SequenceBase::toBool () const {
    bool b = false;
    if (eval_state->parent) {
        sequence = eval_state->sequence;
        Sequence *lst = toSequence ();
        b = lst && lst->first ();
        delete lst;
    } else {
        b = StringBase::toBool ();
    }
    return b;
}

QString SequenceBase::toString () const {
    if (eval_state->sequence != sequence) {
        Sequence *lst = toSequence ();
        int i = lst->length ();
        if (i == 1)
            string = lst->first ()->data.value ();
        else
            string = QString::number (i);
        delete lst;
        sequence = eval_state->sequence;
    }
    return string;
}

AST::Type SequenceBase::type () const {
    QString s = toString ();
    if (s.toLower () == "true" ||
            s.toLower () == "false")
        return TBool;
    bool ok;
    s.toInt (&ok);
    if (ok)
        return TInteger;
    s.toFloat (&ok);
    if (ok)
        return TFloat;
    return TString;
}

Sequence *Step::toSequence () const {
    Sequence *lst = new Sequence;
    NodeValueItem *last = eval_state->process_list->last ();
    Sequence recursive_lst;
    for (NodeValueItem *itm = eval_state->process_list->first (); itm; ) {
        NodeValueItem *next = itm == last ? NULL : itm->nextSibling ();

        Node *n = itm->data.node;
        if (!n)
            continue; // FIXME
        if (is_attr) {
            Element *e = n->isElementNode() ? static_cast<Element *> (n) : NULL;
            Attribute *a = e ? e->attributes ().first () : NULL;
            for (; a; a = a->nextSibling ())
                if (matches (a))
                    lst->append (new NodeValueItem (NodeValue (n, a)));
            if (recursive)
                for (Node *c = n->firstChild(); c; c = c->nextSibling ())
                    recursive_lst.append (new NodeValueItem (c));
        } else if (context_node) {
            if (eval_state->parent)
                lst->append (new NodeValueItem (eval_state->root));
        } else {
            for (Node *c = n->firstChild(); c; c = c->nextSibling ()) {
                if (matches (c))
                    lst->append (new NodeValueItem (c));
                if (recursive)
                    recursive_lst.append (new NodeValueItem (c));
            }
        }

        itm = next;
    }
    if (recursive && recursive_lst.first ()) {
        Sequence *old = eval_state->process_list;
        eval_state->process_list = &recursive_lst;
        Sequence *rlst = toSequence ();
        for (NodeValueItem *r = rlst->first (); r; r = r->nextSibling ())
            lst->append (new NodeValueItem (r->data));
        delete rlst;
        eval_state->process_list = old;
    }
    return lst;
}

Sequence *Path::toSequence () const {
    Sequence *old = eval_state->process_list;

    Sequence *lst = new Sequence;
    EvalState *es = eval_state;
    if (!start_contextual)
        while (es->parent)
            es = es->parent;
    // TODO handle ..
    lst->append (new NodeValueItem (es->root));
    for (AST *s = first_child; s; s = s->next_sibling) {
        eval_state->process_list = lst;
        lst = s->toSequence ();
        delete eval_state->process_list;
    }

    eval_state->process_list = old;
    return lst;
}

Sequence *PredicateFilter::toSequence () const {
    Sequence *lst = first_child->toSequence ();
    Sequence *old = eval_state->process_list;

    for (AST *p = first_child->next_sibling; p; p = p->next_sibling) {
        eval_state->process_list = lst;
        lst = new Sequence;
        for (NodeValueItem *itm = eval_state->process_list->first(); itm; itm = itm->nextSibling()) {
            p->setRoot (itm->data);
            if (p->toBool ())
                lst->append (new NodeValueItem (itm->data));
        }
        delete eval_state->process_list;
    }

    eval_state->process_list = old;
    return lst;
}

QString StringLiteral::toString () const {
    return string;
}

AST::Type StringLiteral::type () const {
    return TString;
}

bool Contains::toBool () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        b = false;
        if (first_child) {
            AST *s = first_child->next_sibling;
            if (s)
                b = first_child->toString ().indexOf (s->toString ()) > -1;
        }
    }
    return b;
}

bool Not::toBool () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        b = first_child ? !first_child->toBool () : true;
    }
    return b;
}

bool StartsWith::toBool () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        b = false;
        if (first_child) {
            AST *s = first_child->next_sibling;
            if (s)
                b = first_child->toString ().startsWith (s->toString ());
            else if (eval_state->parent)
                b = eval_state->root.value ().startsWith (first_child->toString ());
        }
    }
    return b;
}

int Count::toInt () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        if (first_child) {
            Sequence *lst = first_child->toSequence ();
            i = lst->length ();
            delete lst;
        } else {
            i = 0;
        }
    }
    return i;
}

int HoursFromTime::toInt () const {
    if (eval_state->sequence != sequence) {
        if (first_child) {
            QString s = first_child->toString ();
            int p = s.indexOf (':');
            if (p > -1)
                i = s.left (p).toInt ();
        }
        sequence = eval_state->sequence;
    }
    return i;
}

int MinutesFromTime::toInt () const {
    if (eval_state->sequence != sequence) {
        if (first_child) {
            QString s = first_child->toString ();
            int p = s.indexOf (':');
            if (p > -1) {
                int q = s.indexOf (':', p + 1);
                if (q > -1)
                    i = s.mid (p + 1, q - p - 1).toInt ();
            }
        }
        sequence = eval_state->sequence;
    }
    return i;
}

int SecondsFromTime::toInt () const {
    if (eval_state->sequence != sequence) {
        if (first_child) {
            QString s = first_child->toString ();
            int p = s.indexOf (':');
            if (p > -1) {
                p = s.indexOf (':', p + 1);
                if (p > -1) {
                    int q = s.indexOf (' ', p + 1);
                    if (q > -1)
                        i = s.mid (p + 1, q - p - 1).toInt ();
                }
            }
        }
        sequence = eval_state->sequence;
    }
    return i;
}

int Last::toInt () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        if (eval_state->parent) {
            Sequence *lst = eval_state->parent->process_list;
            if (lst)
                i = lst->length ();
        }
    }
    return i;
}

int Number::toInt () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        if (first_child)
            i = first_child->toInt ();
    }
    return i;
}

int Position::toInt () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        if (eval_state->parent) {
            Sequence *lst = eval_state->parent->process_list;
            Node *r = eval_state->root.node;
            QString str = eval_state->root.string;
            if (lst) {
                i = 0;
                for (NodeValueItem *n = lst->first(); n; n = n->nextSibling()) {
                    i++;
                    if ((r && r == n->data.node) ||
                            (!r && str == n->data.string))
                        break;
                }
            }
        }
    }
    return i;
}

int StringLength::toInt () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        if (first_child)
            i = first_child->toString ().length ();
        else if (eval_state->parent)
            i = eval_state->root.value ().length ();
        else
            i = 0;
    }
    return i;
}

QString Concat::toString () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        string.clear ();
        for (AST *child = first_child; child; child = child->next_sibling)
            string += child->toString ();
    }
    return string;
}

QString EscapeUri::toString () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        string.clear ();
        if (first_child)
            string = QUrl::toPercentEncoding (first_child->toString ());
    }
    return string;
}

QString StringJoin::toString () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        string.clear ();
        AST *child = first_child;
        if (child) {
            Sequence *lst = child->toSequence ();
            NodeValueItem *n = lst->first();
            if (n) {
                QString sep;
                if (child->next_sibling)
                    sep = child->next_sibling->toString ();
                string = n->data.value ();
                for (n = n->nextSibling (); n; n = n->nextSibling ())
                    string += sep + n->data.value ();
            }
            delete lst;
        }
    }
    return string;
}

QString SubstringAfter::toString () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        string.clear ();
        AST *child = first_child;
        if (child) {
            AST *next = child->next_sibling;
            if (next) {
                QString s = child->toString ();
                QString t = next->toString ();
                int p = s.indexOf (t);
                if (p > -1)
                    string = s.mid (p + t.length ());
            }
        }
    }
    return string;
}

QString SubstringBefore::toString () const {
    if (eval_state->sequence != sequence) {
        sequence = eval_state->sequence;
        string.clear ();
        AST *child = first_child;
        if (child) {
            AST *next = child->next_sibling;
            if (next) {
                QString s = child->toString ();
                QString t = next->toString ();
                int p = s.indexOf (t);
                if (p > -1)
                    string = s.left (p);
            }
        }
    }
    return string;
}

QString CurrentTime::toString () const {
    if (eval_state->sequence != sequence) {
        char buf[200];
        time_t t = time(NULL);
        struct tm *lt = localtime(&t);
        if (lt && strftime (buf, sizeof (buf), "%H:%M:%S %z", lt))
            string = buf;
        sequence = eval_state->sequence;
    }
    return string;
}

QString CurrentDate::toString () const {
    if (eval_state->sequence != sequence) {
        char buf[200];
        time_t t = time(NULL);
        struct tm *lt = localtime(&t);
        if (lt && strftime (buf, sizeof (buf), "%a, %d %b %Y %z", lt))
            string = buf;
        sequence = eval_state->sequence;
    }
    return string;
}

static void sortList (Sequence *lst, Expression *expr) {
    NodeValueItem *cur = lst->first ();
    Sequence lt;
    Sequence gt;
    expr->setRoot (cur->data.node);
    QString str = expr->toString ();
    for (NodeValueItem *itm = cur->nextSibling (); itm; ) {
        NodeValueItem *next = itm->nextSibling ();
        expr->setRoot (itm->data.node);
        int cmp = str.compare (expr->toString ());
        if (cmp < 0) {
            NodeValueItemPtr s = itm;
            lst->remove (itm);
            gt.append (itm);
        } else if (cmp > 0) {
            NodeValueItemPtr s = itm;
            lst->remove (itm);
            lt.append (itm);
        }
        itm = next;
    }
    if (lt.first ()) {
        sortList (&lt, expr);
        lst->splice (lst->first (), lt);
    }
    if (gt.first ()) {
        sortList (&gt, expr);
        lst->splice (NULL, gt);
    }
}

Sequence *Sort::toSequence () const {
    if (first_child) {
        Expression* exp = evaluateExpr(first_child->toString().toUtf8());
        if (exp) {
            exp->setRoot (eval_state->root.node);
            Sequence *lst = exp->toSequence ();
            if (lst->first () && first_child->next_sibling) {
                Expression *sort_exp =
                    evaluateExpr(first_child->next_sibling->toString().toUtf8());
                if (sort_exp) {
                    sortList (lst, sort_exp);
                    delete sort_exp;
                }
            }
            delete exp;
            return lst;
        }
    }
    return AST::toSequence ();
}

Sequence *SubSequence::toSequence () const {
    Sequence *lst = new Sequence;
    AST *n = first_child;
    if (n) {
        Sequence *src = n->toSequence ();
        n = n->next_sibling;
        if (n) {
            int p = n->toInt ();
            if (p < 1)
                p = 1;
            int len = -1;
            if (n->next_sibling)
                len = n->next_sibling->toInt ();
            NodeValueItem *itm = src->first ();
            for (; itm && --p; itm = itm->nextSibling ())
            {}
            for (; itm && len--; itm = itm->nextSibling ())
                lst->append (new NodeValueItem (itm->data));
        }
        delete src;
    }
    return lst;
}

Sequence *Tokenize::toSequence () const {
    Sequence *lst = new Sequence;
    if (first_child) {
        if (first_child->next_sibling) {
            QString s = first_child->toString ();
            QRegExp r (first_child->next_sibling->toString ());
            int p = 0;
            while (p >= 0) {
                p = r.indexIn (s, p);
                if (p >= 0) {
                    int len = r.matchedLength();
                    lst->append (new NodeValueItem (s.mid (p, len)));
                    p += len;
                }
            }
        }
    }
    return lst;
}

#define BIN_OP_TO_INT(NAME,OP)                                           \
    AST *second_child = first_child->next_sibling;                       \
    AST::Type t1 = first_child->type ();                                 \
    AST::Type t2 = second_child->type ();                                \
    if (AST::TInteger == t1 && AST::TInteger == t2)                      \
        return first_child->toInt() OP second_child->toInt();            \
    if (AST::TInteger == t1 && AST::TFloat == t2)                        \
        return (int) (first_child->toInt() OP                            \
                second_child->toFloat());                                \
    if (AST::TFloat == t1 && AST::TInteger == t2)                        \
        return (int) (first_child->toFloat() OP                          \
                second_child->toInt());                                  \
    if (AST::TFloat == t1 && AST::TFloat == t2)                          \
        return (int) (first_child->toFloat() OP                          \
                second_child->toFloat());                                \
    return 0

int Multiply::toInt () const {
    BIN_OP_TO_INT(multiply, *);
}

float Multiply::toFloat () const {
    return first_child->toFloat () * first_child->next_sibling->toFloat ();
}

static AST::Type binaryASTType (const AST *ast) {
    AST::Type t1 = ast->first_child->type ();
    AST::Type t2 = ast->first_child->next_sibling->type ();
    if (t1 == t2 && (AST::TInteger == t1 || AST::TFloat == t1))
        return t1;
    if ((AST::TInteger == t1 && AST::TFloat == t2) ||
            (AST::TInteger == t2 && AST::TFloat == t1))
        return AST::TFloat;
    return AST::TUnknown;
}

AST::Type Multiply::type () const {
    return binaryASTType (this);
}

#ifdef KMPLAYER_EXPR_DEBUG
void Multiply::dump () const {
    fprintf (stderr, "* [");
    AST::dump();
    fprintf (stderr, " ]");
}
#endif

int Divide::toInt () const {
    BIN_OP_TO_INT(divide,/);
}

float Divide::toFloat () const {
    return first_child->toFloat () / first_child->next_sibling->toFloat ();
}

AST::Type Divide::type () const {
    return binaryASTType (this);
}

#ifdef KMPLAYER_EXPR_DEBUG
void Divide::dump () const {
    fprintf (stderr, "/ [");
    AST::dump();
    fprintf (stderr, " ]");
}
#endif

int Modulus::toInt () const {
    AST::Type t1 = first_child->type ();
    AST::Type t2 = first_child->next_sibling->type ();
    if (t1 == t2 && (TInteger == t1 || TFloat == t1))
        return first_child->toInt () % first_child->next_sibling->toInt ();
    return 0;
}

float Modulus::toFloat () const {
    return toInt ();
}

AST::Type Modulus::type () const {
    AST::Type t1 = first_child->type ();
    AST::Type t2 = first_child->next_sibling->type ();
    if (t1 == t2 && (TInteger == t1 || TFloat == t1))
        return TInteger;
    return TUnknown;
}

#ifdef KMPLAYER_EXPR_DEBUG
void Modulus::dump () const {
    fprintf (stderr, "%% [");
    AST::dump();
    fprintf (stderr, " ]");
}
#endif

int Plus::toInt ()  const{
    BIN_OP_TO_INT(plus,+);
}

float Plus::toFloat () const {
    return first_child->toFloat () + first_child->next_sibling->toFloat ();
}

AST::Type Plus::type () const {
    return binaryASTType (this);
}

#ifdef KMPLAYER_EXPR_DEBUG
void Plus::dump () const {
    fprintf (stderr, "+ [");
    AST::dump();
    fprintf (stderr, " ]");
}
#endif

int Minus::toInt () const {
    return first_child->toInt() - first_child->next_sibling->toInt();
}

float Minus::toFloat () const {
    return first_child->toFloat () - first_child->next_sibling->toFloat ();
}

AST::Type Minus::type () const {
    return binaryASTType (this);
}

#ifdef KMPLAYER_EXPR_DEBUG
void Minus::dump () const {
    fprintf (stderr, "- [");
    AST::dump();
    fprintf (stderr, " ]");
}
#endif

Sequence *Join::toSequence () const {
    if (first_child) {
        Sequence *lst = first_child->toSequence ();
        if (first_child->next_sibling) {
            Sequence *l2 = first_child->next_sibling->toSequence ();
            lst->splice (NULL, *l2);
            delete l2;
        }
        return lst;
    }
    return AST::toSequence ();
}

#ifdef KMPLAYER_EXPR_DEBUG
void Join::dump () const {
    fprintf (stderr, "| [");
    AST::dump();
    fprintf (stderr, " ]");
}
#endif

bool Comparison::toBool () const {
    AST::Type t1 = first_child->type ();
    AST::Type t2 = first_child->next_sibling->type ();
    switch (comp_type) {
    case lt:
        return first_child->toFloat () < first_child->next_sibling->toFloat ();
    case lteq:
        return first_child->toInt () <= first_child->next_sibling->toInt ();
    case gt:
        return first_child->toFloat () > first_child->next_sibling->toFloat ();
    case gteq:
        return first_child->toInt () >= first_child->next_sibling->toInt ();
    case eq:
        if (t1 == AST::TString || t2 == AST::TString)
            return first_child->toString () ==
                first_child->next_sibling->toString ();
        return first_child->toInt () == first_child->next_sibling->toInt ();
    case noteq:
        return first_child->toInt () != first_child->next_sibling->toInt ();
    case land:
        return first_child->toBool () && first_child->next_sibling->toBool ();
    case lor:
        return first_child->toBool () || first_child->next_sibling->toBool ();
    }
    return false;
}

#ifdef KMPLAYER_EXPR_DEBUG
void Comparison::dump () const {
    switch (comp_type) {
    case lt:
        fprintf (stderr, "< [");
        break;
    case lteq:
        fprintf (stderr, "<= [");
        break;
    case gt:
        fprintf (stderr, "> [");
        break;
    case gteq:
        fprintf (stderr, ">= [");
        break;
    case eq:
        fprintf (stderr, "== [");
        break;
    case noteq:
        fprintf (stderr, "!= [");
        break;
    case land:
        fprintf (stderr, "&& [");
        break;
    case lor:
        fprintf (stderr, "|| [");
    }
    AST::dump();
    fprintf (stderr, " ]");
}
#endif


struct Parser {
    enum { TEof=-1, TDouble=-2, TLong=-3, TIdentifier=-4 };

    const char *source;
    const char *cur;

    int cur_token;
    long long_value;
    double double_value;
    QString str_value;
    QString error;

    Parser(const char* s) : source(s), cur(source) {}
    void nextToken();
    void setError(const char* err) {
        fprintf(stderr, "Error at %d: %s\n", cur-source, err);
    }
};

void Parser::nextToken() {
    const char* begin = cur;
    bool is_num = false;
    bool is_fractal = false;
    while (true) {
        char c = *cur;
        switch (c) {
        case 0:
            if (begin == cur) {
                cur_token = TEof;
                return;
            }
            goto interp;
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            if (begin == cur) {
                ++begin;
                break;
            }
            goto interp;
        case '.':
            if (is_num && !is_fractal) {
                is_fractal = true;
                break;
            }
            // fall through
        default:
            if ((is_num || begin == cur) && c >= '0' && c <= '9') {
                is_num = true;
                break;
            }
            if (is_num && !(c >= '0' && c <= '9'))
                goto interp;
            if (!((c >= 'a' && c <= 'z')
                        || (c >= 'A' && c <= 'Z')
                        || (cur != begin && c >= '0' && c <= '9')
                        || c == '_'
                        || (cur != begin && c == '-')
                        || (unsigned char)c > 127)) {
                if (begin == cur) {
                    cur_token = c;
                    ++cur;
                    return;
                }
                goto interp;
            }
        }
        cur++;
    }
interp:
    if (is_num && cur - begin < 63) {
        char buf[64];
        memcpy(buf, begin, cur - begin);
        buf[cur - begin] = 0;
        if (is_fractal) {
            cur_token = TDouble;
            double_value = strtod(buf, NULL);
        } else {
            cur_token = TLong;
            long_value = strtol(buf, NULL, 10);
        }
    } else {
        cur_token = TIdentifier;
        str_value = QString::fromUtf8(QByteArray(begin, cur-begin));
    }
}

static bool parseStatement (Parser *parser, AST *ast);


static bool parsePredicates (Parser *parser, AST *ast) {
    AST pred (new EvalState (ast->eval_state));
    while (true) {
        if (parseStatement (parser, &pred)) {
            if (']' != parser->cur_token)
                return false;
            if (pred.first_child)
                appendASTChild (ast, releaseASTChildren (&pred));
        } else if (']' != parser->cur_token) {
            return false;
        }
        parser->nextToken();
        if ('[' != parser->cur_token)
            break;
        parser->nextToken();
    }
    return true;
}

static bool parseStep (Parser *parser, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    AST *entry = NULL;
    bool recursive = '/' == parser->cur_token;
    if (recursive)
        parser->nextToken();
    if ('.' == parser->cur_token) {
        parser->nextToken();
        if ('.' == parser->cur_token) { // TODO
            parser->nextToken();
            entry = new Step (ast->eval_state, true);
        } else {
            entry = new Step (ast->eval_state, true);
        }
    } else {
        bool is_attr = '@' == parser->cur_token;
        if (is_attr)
            parser->nextToken();
        if ('*' == parser->cur_token)
            entry = new Step (ast->eval_state, "*", is_attr, recursive);
        else if (Parser::TIdentifier == parser->cur_token)
            entry = new Step (ast->eval_state, parser->str_value, is_attr, recursive);
        else
            return false;
        parser->nextToken();

        AST fast (ast->eval_state);
        if ('[' == parser->cur_token) {
            parser->nextToken();
            if (!parsePredicates (parser, &fast))
                return false;
            entry->next_sibling = releaseASTChildren (&fast);
            entry = new PredicateFilter (ast->eval_state, entry);
        }
    }
    appendASTChild (ast, entry);
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s success end:'%s'\n", __FUNCTION__, parser->cur);
#endif
    return true;
}

static bool parsePath (Parser *parser, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    Path path (ast->eval_state, NULL, false);
    bool has_any = false;

    bool start_contextual =  '/' != parser->cur_token;
    if ('/' == parser->cur_token) {
        parser->nextToken();
    } else if (!ast->eval_state->parent
            && !ast->eval_state->def_root_tag.isEmpty ()) {
        appendASTChild (&path, new Step (ast->eval_state,
                    ast->eval_state->def_root_tag,
                  false, false));
    }
    if (parseStep (parser, &path)) {
        has_any = true;
        while ('/' == parser->cur_token) {
            parser->nextToken();
            if (!parseStep (parser, &path))
                break;
        }
    }
    if (has_any) {
        appendASTChild (ast, new Path (ast->eval_state, releaseASTChildren (&path), start_contextual));
#ifdef KMPLAYER_EXPR_DEBUG
        fprintf (stderr, "%s success end:'%s'\n", __FUNCTION__, parser->cur);
#endif
        return true;
    }
    return false;
}

static bool parseFunction (Parser *parser, const QString &name, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    AST fast (ast->eval_state);
    while (Parser::TEof != parser->cur_token) {
        switch (parser->cur_token) {
        case ')': {
            parser->nextToken();
            AST *func = NULL;
            if (name == "concat")
                func = new Concat (ast->eval_state);
            else if (name == "contains")
                func = new Contains (ast->eval_state);
            else if (name == "count")
                func = new Count (ast->eval_state);
            else if (name == "hours-from-time")
                func = new HoursFromTime (ast->eval_state);
            else if (name == "minutes-from-time")
                func = new MinutesFromTime (ast->eval_state);
            else if (name == "seconds-from-time")
                func = new SecondsFromTime (ast->eval_state);
            else if (name == "current-time")
                func = new CurrentTime (ast->eval_state);
            else if (name == "current-date")
                func = new CurrentDate (ast->eval_state);
            else if (name == "last")
                func = new Last (ast->eval_state);
            else if (name == "not")
                func = new Not (ast->eval_state);
            else if (name == "number")
                func = new Number (ast->eval_state);
            else if (name == "position")
                func = new Position (ast->eval_state);
            else if (name == "sort")
                func = new Sort (ast->eval_state);
            else if (name == "starts-with")
                func = new StartsWith (ast->eval_state);
            else if (name == "string-join")
                func = new StringJoin (ast->eval_state);
            else if (name == "string-length")
                func = new StringLength (ast->eval_state);
            else if (name == "subsequence")
                func = new SubSequence (ast->eval_state);
            else if (name == "substring-after")
                func = new SubstringAfter (ast->eval_state);
            else if (name == "substring-before")
                func = new SubstringBefore (ast->eval_state);
            else if (name == "tokenize")
                func = new Tokenize (ast->eval_state);
            else if (name == "escape-uri")
                func = new EscapeUri (ast->eval_state);
            else
                return false;
            appendASTChild (ast, func);
            func->first_child = releaseASTChildren (&fast);
#ifdef KMPLAYER_EXPR_DEBUG
            fprintf (stderr, "%s succes str:'%s'\n", __FUNCTION__, parser->cur);
#endif
            return true;
        }
        case ',':
            parser->nextToken();
            break;
        default:
            if (!parseStatement (parser, &fast))
                return false;
        }
    }
    return false;
}

static bool parseFactor (Parser *parser, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    AST fast (ast->eval_state);
    int sign = 1;
    if ('+' == parser->cur_token || '-' == parser->cur_token) {
        sign = '-' == parser->cur_token ? -1 : 1;
        parser->nextToken();
    }
    switch (parser->cur_token) {
    case Parser::TEof:
        return true;
    case '(':
        parser->nextToken();
        if (!parseStatement (parser, &fast))
            return false;
        if (')' != parser->cur_token)
            return false;
        parser->nextToken();
        break;
    case '.':
    case '@':
    case '*':
    case '/':
        if (!parsePath (parser, &fast))
            return false;
        break;
    case '\'':
    case '"': {
        const char* s = parser->cur;
        while (*s && *s != parser->cur_token)
            ++s;
        if (!*s) {
            parser->setError("expected string literal");
            parser->cur = s;
            parser->cur_token = Parser::TEof;
            return false;
        }
        appendASTChild(&fast, new StringLiteral(ast->eval_state, QString::fromUtf8(QByteArray(parser->cur, s - parser->cur))));
        parser->cur = ++s;
        parser->nextToken();
#ifdef KMPLAYER_EXPR_DEBUG
        fprintf (stderr, "%s success end:'%s' cur token %c\n", __FUNCTION__, parser->cur, parser->cur_token);
#endif
        break;
    }
    case Parser::TDouble:
        appendASTChild (&fast, new Float (ast->eval_state, (float)(sign * parser->double_value)));
        parser->nextToken();
        break;
    case Parser::TLong:
        appendASTChild (&fast,  new Integer (ast->eval_state, (int)(sign * parser->long_value)));
        parser->nextToken();
        break;
    case Parser::TIdentifier: {
        QString str = parser->str_value;
        const char* cur = parser->cur;
        parser->nextToken();
        if ('(' == parser->cur_token) {
            parser->nextToken();
            if (!parseFunction (parser, str, &fast))
                return false;
        } else {
            parser->cur = cur;
            parser->cur_token = Parser::TIdentifier;
            parser->str_value = str;
            if (!parsePath (parser, &fast))
                return false;
        }
        break;
    }
    default:
        return false;
    }
    if ('[' == parser->cur_token) {
        parser->nextToken();
        if (!parsePredicates (parser, &fast))
            return false;
        appendASTChild (ast,
                new PredicateFilter (ast->eval_state, releaseASTChildren (&fast)));
    } else {
        appendASTChild (ast, releaseASTChildren (&fast));
    }
    return true;
}

static bool parseTerm (Parser *parser, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    if (parseFactor (parser, ast)) {
        while (true) {
            int op = 0;
            if ('*' == parser->cur_token) {
                op = '*';
            } else if (Parser::TIdentifier == parser->cur_token) {
                if ( parser->str_value == "div")
                    op = '/';
                else if ( parser->str_value == "mod")
                    op = '%';
            }
            if (!op)
                break;
            parser->nextToken();
            AST tmp (ast->eval_state);
            if (parseFactor (parser, &tmp)) {
                AST *chlds = releaseLastASTChild (ast);
                chlds->next_sibling = releaseASTChildren (&tmp);
                appendASTChild (ast,
                        op == '*'
                        ? (AST *) new Multiply (ast->eval_state, chlds)
                        : op == '/'
                        ? (AST *) new Divide (ast->eval_state, chlds)
                        : (AST *) new Modulus (ast->eval_state, chlds));
            } else {
                parser->setError("expected factor");
                return false;
            }
        }
#ifdef KMPLAYER_EXPR_DEBUG
        fprintf (stderr, "%s success end:'%s'\n", __FUNCTION__, parser->cur);
#endif
        return true;
    }
    return false;
}

static bool parseExpression (Parser *parser, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    if (parseTerm (parser, ast)) {
        while (true) {
            int op = parser->cur_token;
            if (op != '+' && op != '-' && op != '|')
                break;
            parser->nextToken();
            AST tmp (ast->eval_state);
            if (parseTerm (parser, &tmp)) {
                AST *chlds = releaseLastASTChild (ast);
                chlds->next_sibling = releaseASTChildren (&tmp);
                appendASTChild (ast, op == '+'
                        ? (AST *) new Plus (ast->eval_state, chlds)
                        :  op == '-'
                        ? (AST *) new Minus (ast->eval_state, chlds)
                        : (AST *) new Join (ast->eval_state, chlds));
            } else {
                parser->setError("expected term");
                return false;
            }
        }
#ifdef KMPLAYER_EXPR_DEBUG
        fprintf (stderr, "%s success end:'%s'\n", __FUNCTION__, parser->cur);
#endif
        return true;
    }
    return false;
}

static bool parseStatement (Parser *parser, AST *ast) {
#ifdef KMPLAYER_EXPR_DEBUG
    fprintf (stderr, "%s enter str:'%s'\n", __FUNCTION__, parser->cur);
#endif
    if (parseExpression (parser, ast)) {
        bool skip_next_token = false;
        enum EComparison {
            err=-1, asign,
            lt=Comparison::lt, lteq, eq, noteq, gt, gteq, land, lor
        } comparison = err;
        switch (parser->cur_token) {
        case '<':
            parser->nextToken();
            if (parser->cur_token == '=') {
                comparison = lteq;
            } else {
                skip_next_token = true;
                comparison = lt;
            }
            break;
        case '>':
            parser->nextToken();
            if ('=' == parser->cur_token) {
                comparison = gteq;
            } else {
                skip_next_token = true;
                comparison = gt;
            }
            break;
        case '=':
            comparison = eq;
            break;
        case '!':
            parser->nextToken();
            if ('=' == parser->cur_token) {
                parser->setError("expected =");
                return false;
            }
            comparison = noteq;
            break;
        case Parser::TIdentifier:
            if (parser->str_value == "and")
                comparison = land;
            else if (parser->str_value == "or")
                comparison = lor;
            break;
        default:
            return true;
        }
        AST tmp (ast->eval_state);
        if (!skip_next_token)
            parser->nextToken();
        if (parseExpression (parser, &tmp)) {
            AST *chlds = releaseLastASTChild (ast);
            chlds->next_sibling = releaseASTChildren (&tmp);
            appendASTChild (ast, new Comparison (ast->eval_state,
                        (Comparison::CompType)comparison, chlds));
        } else {
            parser->setError("expected epression");
            return false;
        }

#ifdef KMPLAYER_EXPR_DEBUG
        fprintf (stderr, "%s success end:'%s'\n", __FUNCTION__, parser->cur);
#endif
        return true;
    }
    return false;
}

Expression* KMPlayer::evaluateExpr(const QByteArray& expr, const QString &root) {
    EvalState *eval_state = new EvalState (NULL, root);
    AST ast (eval_state);
    Parser parser(expr.constData());
    parser.nextToken ();
    if (parseStatement (&parser, &ast)) {
#ifdef KMPLAYER_EXPR_DEBUG
        ast.dump();
        fprintf (stderr, "\n");
#endif
        return releaseASTChildren (&ast);
    }
    return NULL;
}
/*
int main (int argc, char **argv) {
    AST ast;
    const char *end;
    if (argc < 2) {
        fprintf (stderr, "Usage %s <expr>\n", argv[0]);
        return 1;
    }
    printf ("expr '%s' parsed:%d\n", argv[1], parseStatement (argv[1], &end, &ast));
    ast.dump();
    if (ast.first_child)
        printf ("\ni:%d f:%.4f b:%d s:%s\n",
                ast.first_child->toInt(), ast.first_child->toFloat(), ast.first_child->toBool(), ast.first_child->toString());
    return 0;
}*/
