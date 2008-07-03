
module Main (main) where

import System.Environment (getArgs)
import System.Exit        (exitFailure)
import System.IO

import MScript.Lex
import MScript.Par
import MScript.Abs
import MScript.ErrM

import MScript.Print
import Char (isSpace)

import TypeChecker (typecheck)

myRender :: Doc -> String
myRender d = rend 0 (map ($ "") $ d []) "" where
  rend i ss = case ss of
    "["      :ts -> showChar '[' . rend i ts
    "("      :ts -> showChar '(' . rend i ts
    "{"      :ts -> showChar '{' . new (i+1) . rend (i+1) ts
    "}"      :ts -> new (i-1) . showChar '}' . new (i-1) . rend (i-1) ts
    "."      :ts -> showChar '.' . new i . rend i ts
    t  : "." :ts -> showString t . showChar '.' . new i . rend i ts
    t  : "," :ts -> showString t . space "," . rend i ts
    t  : ")" :ts -> showString t . showChar ')' . rend i ts
    t  : "]" :ts -> showString t . space "]" . rend i ts
    t        :ts -> space t . rend i ts
    _            -> id
  new i   = showChar '\n' . replicateS (2*i) (showChar ' ') . dropWhile isSpace
  space t = showString t . (\s -> if null s then "" else (' ':s))

myPrintTree = myRender . prt 0

runTypeChecker :: String -> IO ()
runTypeChecker s =
    case pClassFile (myLexer s) of
      Bad e -> do hPutStrLn stderr "Parse error:\n"
                  hPutStrLn stderr e
                  exitFailure
      Ok  c -> case typecheck c of
                 Left e -> do hPutStrLn stderr "Type error:\n"
                              hPutStrLn stderr (show e)
                              exitFailure
                 Right c' -> do hPutStrLn stderr "OK:\n"
                                hPutStrLn stderr (myPrintTree c')

main :: IO ()
main =
    do args <- getArgs
       case args of
         [file] -> readFile file >>= runTypeChecker
         _      -> do hPutStrLn stderr "usage: check <file>"
                      exitFailure
