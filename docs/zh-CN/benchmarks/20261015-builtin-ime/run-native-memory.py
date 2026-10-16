from pathlib import Path
import argparse,subprocess,shlex,json
parser=argparse.ArgumentParser(description='Compare the frozen native keyboard baseline with the current shared frontend (Ninja build).')
parser.add_argument('--build',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);options=parser.parse_args()
sdk=Path(__file__).resolve().parents[4];assets=Path(__file__).resolve().parent
task=options.output.resolve();task.mkdir(parents=True,exist_ok=True);build=options.build.resolve()
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','pxsys_reference_ime_test'],text=True).splitlines()
recipe=next(x for x in commands if ' -c ' in x and '/src/reference_ime.c' in x);args=shlex.split(recipe);args=args[:args.index('-MD')]
args+=['-I'+str(sdk/'tools/benchmarks'),'-UNDEBUG']
link=shlex.split(commands[-1]);start=link.index('&&')+1;end=link.index('&&',start);link=[s for s in link[start:end] if not s.endswith('.o')]
values=[]
for name in ('before','after'):
 obj=task/(name+'-native-memory.o');exe=task/(name+'-native-memory');flags=[];objects=[str(obj)]
 if name=='before':
  old=assets/'baseline';flags=['-I'+str(old/'include'),'-DIME_BEFORE']
  oldobj=task/'before-native-wrapper.o';subprocess.run([args[0],*flags,*args[1:],'-o',str(oldobj),'-c',str(old/'src/reference_ime.c')],cwd=build,check=True);objects.append(str(oldobj))
 subprocess.run([args[0],*flags,*args[1:],'-o',str(obj),'-c',str(assets/'native-memory.c')],cwd=build,check=True)
 command=link.copy();command[command.index('-o')+1]=str(exe);command[1:1]=objects;command+=['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free']
 subprocess.run(command,cwd=build,check=True)
 for shape in (0,1):
  output=subprocess.check_output([str(exe),str(shape)],text=True);row=json.loads(output.splitlines()[-1]);row['variant']=name;values.append(row);print(row,flush=True)
(task/'native-memory.json').write_text(json.dumps(values,indent=2)+'\n')
