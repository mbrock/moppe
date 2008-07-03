
\documentclass{article}
%include polycode.fmt

%subst comment a = "\mbox{\small --- " a "}"
%subst keyword a = "\mbox{\bf " a "}"
%format { = "\mskip5mu\{\mskip1.5mu"

%usepackage[boldsans]{ccfonts}
%%usepackage{parskip}

\begin{document}

\title{\Huge{MScript Typechecker}}
\date{July 2008}
\author{Mikael Brockman}

\section{Things}

> {-# LANGUAGE TypeSynonymInstances #-}

> module TypeChecker (typecheck) where

> import MScript.Abs
> import MScript.Print
> import MScript.ErrM
> 
> import Control.Monad
> import Control.Monad.Trans
> import Control.Monad.State
> import Control.Monad.Error

> class Name a where
>     name :: a -> String
> 
> instance Name String where
>     name = id
> 
> instance Name ID where
>     name (ID (_, s)) = s

> type TC a = ErrorT TypeError (State Environment) a
> 
> data Environment =
>     MkEnv {  envCurrentClass       ::  String
>           ,  envCurrentClassScope  ::  ClassScope }
> 
> setCurrentClass :: Name a => a -> TC ()
> setCurrentClass s  = modify (\e -> e {  envCurrentClass = name s })
> 
> getCurrentClass :: TC String
> getCurrentClass  = gets               envCurrentClass

> data TypeError  =  XInternalError String
>                 |  XNoError
>                    deriving Show
> 
> instance Error TypeError where
>     noMsg   = XNoError
>     strMsg  = XInternalError

> data ClassScope = Null

> typecheck :: ClassFile -> Either TypeError ClassFile
> typecheck c =
>     let e = MkEnv {  envCurrentClass       = undefined,
>                      envCurrentClassScope  = Null } in
>       case runState (runErrorT (checkClassFile c)) e of
>         (Left e,  _)  ->  Left e
>         (Right x, _)  ->  return x

> checkClassFile :: ClassFile -> TC ClassFile
> checkClassFile (ClassFile id clauses) =
>     do  setCurrentClass id
>         return $ ClassFile id clauses

\end{document}