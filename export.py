#!/usr/bin/python

import shutil
import os

inputDir="./"
prefix="Z:\Software"
lib=os.path.join(prefix,"lib")
include=os.path.join(prefix,"include")
basename="Error"

for folder in ("MSP430 printf Release","MSP430 printf Debug","MSP430 SDcard Release","MSP430 SDcard Debug"):
    outname=basename+"_"+"_".join(folder.split()[1:])+".hza"
    outpath=os.path.join(lib,outname)
    inpath=os.path.join(inputDir,os.path.join(folder,basename+".hza"))
    print("Copying "+inpath+" to "+outpath)
    shutil.copyfile(inpath,outpath)

for file in ("Error.h",):
    outpath=os.path.join(include,file)
    inpath=os.path.join(inputDir,file)
    print("Copying "+inpath+" to "+outpath)
    shutil.copyfile(inpath,outpath)

