#!/usr/bin/python
# -*- coding: utf-8 -*-

import sys
import os
import getopt
import time
import random
import signal
import subprocess
import shutil

progdir = os.path.abspath(os.path.join(os.path.dirname( __file__ )))
pinbin = "/home/orhan/Desktop/Githup/RegionPinFI/pin"
instcategorylib = "/home/orhan/Desktop/Githup/RegionPinFI/source/tools/pinfi/obj-intel64/instcategory.so"
instcountlib = "/home/orhan/Desktop/Githup/RegionPinFI/source/tools/pinfi/obj-intel64/instcount.so"
filib = "/home/orhan/Desktop/Githup/RegionPinFI/source/tools/pinfi/obj-intel64/faultinjection.so"
#inputfile = progdir + "/input.txt"

timeout = 1000

optionlist = []

def remove_files(mkdir):
  # Get the list of all files and directories in the directory
  for file_name in os.listdir(mkdir):
      file_path = os.path.join(mkdir, file_name)
      
      try:
          # If the file is a directory (folder), delete it along with its contents using shutil.rmtree
          if os.path.isdir(file_path):
              shutil.rmtree(file_path)
          # If the file is a file, delete it using os.remove
          else:
              os.remove(file_path)
      except Exception as e:
          print("Error: An error occurred while deleting {}. Error message: {}".format(file_path, e))

def compare_files(goldenfile_path, outputfile_path):
  with open(goldenfile_path, 'rb') as golden_file, open(outputfile_path, 'rb') as output_file:
      golden_content = golden_file.readlines()
      output_content = output_file.readlines()
  
  if golden_content == output_content:
    return True
  else:
    return False

def write_FI_result(run_number, success_count, sdc_count, crash_count):
  resultfile = pinfidir + "/resultFI__" + str(application_name) + "__" + str(function_name)
  result_file = open(resultfile, 'w')
  result_file.write("Application name: " + str(application_name))
  result_file.write("\nFI function name: " + str(function_name))
  result_file.write("\nNumber of FI tested: " + str(run_number))
  result_file.write("\n#of Success: " + str(success_count))
  result_file.write("\n#of SDC: " + str(sdc_count))
  result_file.write("\n#of Crash: " + str(crash_count))
  result_file.close()

# execute pin fo FI
def execute( execlist):
	#print "Begin"
	#inputFile = open(inputfile, "r")
  global outputfile
  print(' '.join(execlist))
  #print outputfile
  outputFile = open(outputfile, "w")
  p = subprocess.Popen(execlist, stdout = outputFile)
  elapsetime = 0
  while (elapsetime < timeout):
    elapsetime += 1
    time.sleep(1)
    #print p.poll()
    if p.poll() is not None:
      print("\t program finish", p.returncode)
      print("\t time taken", elapsetime, "\n")
      #outputFile = open(outputfile, "w")
      #outputFile.write(p.communicate()[0])
      outputFile.close()
      #inputFile.close()
      return str(p.returncode)
  #inputFile.close()
  outputFile.close()
  print("\tParent : Child timed out. Cleaning up ... ")
  p.kill()
  return "timed-out"
	#should never go here
  sys.exit(syscode)


def main():

  # Delete previously created error, output, and result files
  remove_files(errordir)
  remove_files(outputdir)

  #clear previous output
  global run_number, optionlist, outputfile
  outputfile = basedir + "/golden_output"
  execlist = [pinbin, '-t', instcategorylib, '--', progbin]
  execlist.extend(optionlist)
  execute(execlist)

  # baseline
  outputfile = basedir + "/golden_output"
  execlist = [pinbin, '-t', instcountlib, '-func', function_name, '--', progbin]
  execlist.extend(optionlist)
  execute(execlist)

  # fault injection
  success_count = 0
  crash_count = 0
  sdc_count = 0

  for index in range(0, run_number):
    isCrash = False
    print("Running fault injection number " + str(1 + index) + " of " + str(run_number))
    outputfile = outputdir + "/outputfile-" + str(index)
    errorfile = errordir + "/errorfile-" + str(index)
    execlist = [pinbin, '-t', filib, '-fioption', 'AllInst', '-func', function_name, '--', progbin]
    execlist.extend(optionlist)
    ret = execute(execlist)
    if ret == "timed-out":
      error_File = open(errorfile, 'w')
      error_File.write("Program hang\n")
      error_File.close()
      isCrash = True
    elif int(ret) < 0:
      error_File = open(errorfile, 'w')
      error_File.write("Program crashed, terminated by the system, return code " + ret + '\n')
      error_File.close()
      isCrash = True
    elif int(ret) > 0:
      error_File = open(errorfile, 'w')
      error_File.write("Program crashed, terminated by itself, return code " + ret + '\n')
      error_File.close()
      isCrash = True

    # If a crash occurred
    if(isCrash):
      crash_count += 1

    # Check whether it is Success or SDC
    else:
      goldenfile = basedir + "/golden_output"
      # The generated output file is compared with the golden file, which is the actual output of the program, to determine success or SDC.
      if(compare_files(goldenfile, outputfile)):
        success_count += 1
      else:
        sdc_count += 1

  # FI results are written to the file
  write_FI_result(run_number, success_count, sdc_count, crash_count)

if __name__=="__main__":
  global application_name, function_name, run_number
  if len(sys.argv) != 4:
      print("Format Usage: application_name, function_name and program fi_number")
      sys.exit(1)
  application_name = sys.argv[1]
  function_name = sys.argv[2]
  run_number = int(sys.argv[3])
  progbin = progdir + "/" + str(application_name)

  #check application is exist given path
  if not os.path.isfile(progbin):
    print(f"No such file or directory of application: {progbin}")
    sys.exit(1)
  
  #pinfi result path
  pinfidir = progdir + "/pinfi_resultFI_" + str(application_name) + "[" + str(function_name) + "]"
  outputdir = pinfidir + "/prog_output"
  basedir = pinfidir + "/baseline"
  errordir = pinfidir + "/error_output"

  #create pinfi result folders
  if not os.path.isdir(pinfidir):
    os.mkdir(pinfidir)
  if not os.path.isdir(outputdir):
    os.mkdir(outputdir)
  if not os.path.isdir(basedir):
    os.mkdir(basedir)
  if not os.path.isdir(errordir):
    os.mkdir(errordir)

  main()
