#include <map>
#include <iostream>

#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "parser.hh"
#include "nixexpr-ast.hh"
#include "util.hh"
#include "xml-writer.hh"
#include "get-drvs.hh"

using namespace nix;


void printHelp()
{
    std::cout << "Syntax: eval-jobs <expr>\n";
}


Expr evalAttr(EvalState & state, Expr e)
{
    return e ? evalExpr(state, e) : e;
}


static void findJobs(EvalState & state, XMLWriter & doc,
    const ATermMap & argsUsed, const ATermMap & argsLeft,
    Expr e, const string & attrPath);


static void tryJobAlts(EvalState & state, XMLWriter & doc,
    const ATermMap & argsUsed, const ATermMap & argsLeft,
    const string & attrPath, Expr fun,
    ATermList formals, const ATermMap & actualArgs)
{
    if (formals == ATempty) {
        findJobs(state, doc, argsUsed, argsLeft,
            makeCall(fun, makeAttrs(actualArgs)), attrPath);
        return;
    }

    Expr name, def; ATerm def2; ATermList values;
    if (!matchFormal(ATgetFirst(formals), name, def2)) abort();
    
    if ((values = (ATermList) argsLeft.get(name))) {

        for (ATermIterator i(ATreverse(values)); i; ++i) {
            ATermMap actualArgs2(actualArgs);
            ATermMap argsUsed2(argsUsed);
            ATermMap argsLeft2(argsLeft);
            actualArgs2.set(name, makeAttrRHS(*i, makeNoPos()));
            argsUsed2.set(name, *i);
            argsLeft2.remove(name);
            tryJobAlts(state, doc, argsUsed2, argsLeft2, attrPath, fun, ATgetNext(formals), actualArgs2);
        }
        
    }
    else if (!matchDefaultValue(def2, def)) 
        throw TypeError(format("cannot auto-call a function that has an argument without a default value (`%1%')")
            % aterm2String(name));
    else
        tryJobAlts(state, doc, argsUsed, argsLeft, attrPath, fun, ATgetNext(formals), actualArgs);
}


static void showArgsUsed(XMLWriter & doc, const ATermMap & argsUsed)
{
    foreach (ATermMap::const_iterator, i, argsUsed) {
        XMLAttrs xmlAttrs2;
        xmlAttrs2["name"] = aterm2String(i->key);
        xmlAttrs2["value"] = showValue(i->value);
        doc.writeEmptyElement("arg", xmlAttrs2);
    }
}

    
static void findJobsWrapped(EvalState & state, XMLWriter & doc,
    const ATermMap & argsUsed, const ATermMap & argsLeft,
    Expr e, const string & attrPath)
{
    debug(format("at path `%1%'") % attrPath);
    
    e = evalExpr(state, e);

    ATermList as, es, formals;
    ATermBool ellipsis;
    ATerm pat, body, pos;
    string s;
    PathSet context;
    
    if (matchAttrs(e, as)) {
        ATermMap attrs;
        queryAllAttrs(e, attrs);

        DrvInfo drv;
        
        if (getDerivation(state, e, drv)) {
            XMLAttrs xmlAttrs;
            Path outPath, drvPath;

            xmlAttrs["name"] = attrPath;
            xmlAttrs["system"] = drv.system;
            xmlAttrs["drvPath"] = drv.queryDrvPath(state);
            xmlAttrs["outPath"] = drv.queryOutPath(state);
            xmlAttrs["description"] = drv.queryMetaInfo(state, "description");
            xmlAttrs["longDescription"] = drv.queryMetaInfo(state, "longDescription");
            xmlAttrs["license"] = drv.queryMetaInfo(state, "license");
            xmlAttrs["homepage"] = drv.queryMetaInfo(state, "homepage");
        
            XMLOpenElement _(doc, "job", xmlAttrs);
            showArgsUsed(doc, argsUsed);
        }

        else {
            foreach (ATermMap::const_iterator, i, attrs)
                findJobs(state, doc, argsUsed, argsLeft, i->value,
                    (attrPath.empty() ? "" : attrPath + ".") + aterm2String(i->key));
        }
    }

    else if (matchFunction(e, pat, body, pos) && matchAttrsPat(pat, formals, ellipsis)) {
        tryJobAlts(state, doc, argsUsed, argsLeft, attrPath, e, formals, ATermMap());
    }

    else
        throw TypeError(format("unknown value: %1%") % showValue(e));
}


static void findJobs(EvalState & state, XMLWriter & doc,
    const ATermMap & argsUsed, const ATermMap & argsLeft,
    Expr e, const string & attrPath)
{
    try {
        findJobsWrapped(state, doc, argsUsed, argsLeft, e, attrPath);
    } catch (Error & e) {
        XMLAttrs xmlAttrs;
        xmlAttrs["location"] = attrPath;
        xmlAttrs["msg"] = e.msg();
        XMLOpenElement _(doc, "error", xmlAttrs);
        showArgsUsed(doc, argsUsed);
    }
}


void run(Strings args)
{
    EvalState state;
    Path releaseExpr;
    ATermMap autoArgs;
    
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--arg" || arg == "--argstr") {
            /* This is like --arg in nix-instantiate, except that it
               supports multiple versions for the same argument.
               That is, autoArgs is a mapping from variable names to
               *lists* of values. */
            if (i == args.end()) throw UsageError("missing argument");
            string name = *i++;
            if (i == args.end()) throw UsageError("missing argument");
            string value = *i++;
            Expr e = arg == "--arg"
                ? parseExprFromString(state, value, absPath("."))
                : makeStr(value);
            autoArgs.set(toATerm(name), (ATerm) ATinsert(autoArgs.get(toATerm(name))
                    ? (ATermList) autoArgs.get(toATerm(name))
                    : ATempty, e));
        }
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%'") % arg);
        else
            releaseExpr = arg;
    }

    store = openStore();

    Expr e = parseExprFromFile(state, releaseExpr);

    XMLWriter doc(true, std::cout);
    XMLOpenElement root(doc, "jobs");
    findJobs(state, doc, ATermMap(), autoArgs, e, "");
}


string programId = "eval-jobs";
