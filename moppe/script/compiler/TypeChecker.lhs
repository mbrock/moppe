
\documentclass{article}
%include polycode.fmt

%subst comment a = "\mbox{\small --- " a "}"
%subst keyword a = "\mbox{\bf " a "}"
%format { = "\mskip5mu\{\mskip1.5mu"

\usepackage[boldsans]{ccfonts}
\usepackage{parskip}

\begin{document}

\title{\Huge{MScript Typechecker}}
\date{July 2008}
\author{Mikael Brockman}

\section{Typechecker Framework}

%if false

> {-# LANGUAGE TypeSynonymInstances #-}

%endif

> module TypeChecker (typecheck) where
>
> -- BNFC-generated modules.
> import MScript.Abs
> import MScript.Print
> import MScript.ErrM
> 
> -- For the typechecker action monad.
> import Control.Monad
> import Control.Monad.Trans
> import Control.Monad.State
> import Control.Monad.Error
>
> -- Utilities.
> import Data.List
>
> import qualified  Data.Map as  Map
> import            Data.Map     (Map)

 A typechecker action operates on a state, called its environment,
 and can potentially fail giving a type error.

> -- The type of typechecker actions.
> type TC a = ErrorT TypeError (StateT Environment IO) a

 The environment holds data relevant throughout the typechecker.

> -- The typechecker's state.
> data Environment = MkEnv  {
>     -- The name of the class being checked.
>     envCurrentClassName  ::  String,
>     -- The class definition so far.
>     envCurrentClassDefn  ::  ClassDefn
> }

\subsection{Quering and Updating the Environment}

 Many syntactic types contain names.  We define a typeclass to make
 them easier to extract.

> class Named a where
>     name :: a -> String
> 
> instance Named String where 
>     name                    = id
> instance Named ID where 
>     name (ID (_, s))        = s
> instance Named IDWithType where 
>     name (IDWithType id _)  = name id
> instance Named Keyword where 
>     name (Keyword (_, s))   = s
> instance Named DeclKeyword where 
>     name (DeclKeyword k _)  = name k

 Here are some accessors.

> getCurrentClassName  ::  TC String
> getCurrentClassName  =   gets envCurrentClassName

> getCurrentClassDefn  ::  TC ClassDefn
> getCurrentClassDefn  =   gets envCurrentClassDefn

> modifyClassDefn :: (ClassDefn -> ClassDefn) -> TC ()
> modifyClassDefn f = 
>     modify (\x -> x { envCurrentClassDefn = 
>                         f (envCurrentClassDefn x) })

> registerMethod :: Selector -> MethodType -> TC ()
> registerMethod s t = modifyClassDefn f
>     where f x = x { classMethods = Map.insert s t (classMethods x) }


\subsection{Type Errors}

> data TypeError  =  XInternalError String
>                 |  XNoError
>                 |  XSelectorAlreadyDefined Selector
>                    deriving Show
> 
> -- {\sc Error} instance so we can use it with {\sc ErrorT}.
> instance Error TypeError where
>     noMsg   =  XNoError
>     strMsg  =  XInternalError

\subsection{Language Types}

> -- Represents method selectors.
> data Selector  =  -- E.g., {\it widget frob.}
>                   UnarySelector    String
>                   -- E.g., {\it widget * 2.}
>                |  BinarySelector   String
>                   -- E.g., {\it widget frobWith: foo and: bar.}
>                |  KeywordSelector  [String]
>                   deriving (Eq, Ord, Show)

> -- A method's type is the type of its return value and parameters.
> newtype MethodType  =  MkMethodType (Type, [Type])
>
> methodReturnType  (MkMethodType (x, _))  =  x
> methodParamTypes  (MkMethodType (_, y))  =  y

> -- The signature of a class.
> data ClassDefn = MkClassDefn  { 
>     classStaticVariables  ::  Map String    Type,
>     classMemberVariables  ::  Map String    Type,
>     classMethods          ::  Map Selector  MethodType
> }

> -- A class definition with no entries.
> emptyClassDefn  =   MkClassDefn e e e
>     where e = Map.empty

> -- This is the module entry point.  It typechecks a class file, giving
> -- either a type error or a type-annotated syntax tree.
> typecheck :: ClassFile -> IO (Either TypeError ClassFile)
> typecheck c@(ClassFile id _) =
>     let e = MkEnv {  envCurrentClassName  =  name id,
>                      envCurrentClassDefn  =  emptyClassDefn } in
>       liftM fst $ runStateT (runErrorT $ checkClassFile c) e

> checkClassFile :: ClassFile -> TC ClassFile
> checkClassFile (ClassFile id clauses) =
>     do  mapM_ checkMethodSignature clauses
>         checkClassVars clauses
>         return $ ClassFile id clauses

> -- Remove the type information from a declaration selector,
> -- giving a regular selector.
> selectorOfDSel :: DeclSelector -> Selector
> selectorOfDSel (DSelUnary      id)  = UnarySelector    (name id)
> selectorOfDSel (DSelBinary   _ id)  = BinarySelector   (name id)
> selectorOfDSel (DSelKeyword  ks)    = KeywordSelector  (map name ks)

> -- Get the parameter types of a declaration selector.
> dselTypes :: DeclSelector -> [Type]
> dselTypes (DSelUnary    _)                   = []
> dselTypes (DSelBinary   _ (IDWithType _ t))  = [t]
> dselTypes (DSelKeyword  ks)                  =
>     map (\(DeclKeyword _ (IDWithType _ t)) -> t) ks

> -- Typecheck and register a method signature, ignoring the body.
> checkMethodSignature :: TopLevelClause -> TC ()
> checkMethodSignature (MethodClause rt ds _) =
>     do  let s = selectorOfDSel ds
>         ms <- liftM (Map.keys . classMethods) getCurrentClassDefn
>         when (s `elem` ms) (throwError $ XSelectorAlreadyDefined s)
>         registerMethod s (MkMethodType (rt, dselTypes ds))
> checkMethodSignature _ = return ()

> checkClassVars :: [TopLevelClause] -> TC ()
> checkClassVars clauses =
>     do  return ()

\end{document}