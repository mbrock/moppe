
module TypeChecker (typecheck) where

import MScript.Abs
import MScript.Print
import MScript.ErrM

import Control.Monad
import Control.Monad.Trans
import Control.Monad.State
import Control.Monad.Error

type TC a = ErrorT TypeError (State Environment) a

data Environment = MkEnv { eFoo :: Int }

data TypeError = XInternalError String
               | XNoError
                 deriving Show

instance Error TypeError where
    noMsg  = XNoError
    strMsg = XInternalError

typecheck :: ClassFile -> Either TypeError ClassFile
typecheck c = return c
