{
    This file is part of the Free Pascal run time library.
    Nintendo 3DS (CTR) target: thread manager on top of libctru threads.

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************}

{ Thread priorities: FPC uses -15..+15 with 0 = normal and larger = higher
  priority; the 3DS kernel uses 0x18..0x3F with smaller = higher. Pascal
  priorities are mapped to an offset from the main thread priority. }

type
  PCtrThreadRec = ^TCtrThreadRec;
  TCtrThreadRec = record
    Handle: Pointer;           { libctru Thread }
    Func: TThreadFunc;
    Param: Pointer;
    StackLen: PtrUInt;
    Started: Pointer;          { event: Handle has been stored }
    SelfClosed: LongBool;
  end;

const
  CtrThreadVarBlockSize: DWord = 0;

var
  CtrMainThreadRec: TCtrThreadRec;

threadvar
  CtrCurrentThread: PCtrThreadRec;

{*****************************************************************************
                             Threadvar support
*****************************************************************************}

procedure CtrInitThreadVar(var offset: DWord; size: DWord);
begin
  CtrThreadVarBlockSize:=align(CtrThreadVarBlockSize,8);
  offset:=CtrThreadVarBlockSize;
  inc(CtrThreadVarBlockSize,size);
end;

procedure CtrAllocateThreadVars;
var
  block: Pointer;
begin
  { allocate from C: the FPC heap manager uses exceptions, which use
    threadvars, which are not allocated yet }
  block:=ctr_malloc(CtrThreadVarBlockSize);
  FillChar(block^,CtrThreadVarBlockSize,0);
  ctr_tls_set(block);
end;

function CtrRelocateThreadVar(offset: DWord): Pointer;
var
  block: Pointer;
begin
  block:=ctr_tls_get;
  if block=nil then
    begin
      { a thread not started via BeginThread (e.g. a libctru callback) }
      CtrAllocateThreadVars;
      InitThread(1000000000);
      block:=ctr_tls_get;
    end;
  CtrRelocateThreadVar:=block+offset;
end;

procedure CtrReleaseThreadVars;
var
  block: Pointer;
begin
  block:=ctr_tls_get;
  if block<>nil then
    begin
      ctr_tls_set(nil);
      ctr_cfree(block);
    end;
end;

{*****************************************************************************
                             Thread starting
*****************************************************************************}

procedure CtrThreadMain(arg: Pointer); cdecl;
var
  rec: PCtrThreadRec;
  func: TThreadFunc;
  param: Pointer;
  res: PtrInt;
begin
  rec:=PCtrThreadRec(arg);
  { wait until BeginThread stored the libctru handle }
  ctr_event_wait(rec^.Started);
  CtrAllocateThreadVars;
  InitThread(rec^.StackLen);
  CtrCurrentThread:=rec;
  func:=rec^.Func;
  param:=rec^.Param;
  res:=func(param);
  { EndThread performs the cleanup and does not return }
  EndThread(DWord(res));
end;

function CtrBeginThread(sa: Pointer; stacksize: PtrUInt;
                        ThreadFunction: TThreadFunc; p: Pointer;
                        creationFlags: DWord; var ThreadId: TThreadID): TThreadID;
var
  rec: PCtrThreadRec;
begin
  CtrBeginThread:=0;
  IsMultiThread:=true;
  rec:=ctr_malloc(sizeof(TCtrThreadRec));
  if rec=nil then
    exit;
  FillChar(rec^,sizeof(rec^),0);
  rec^.Func:=ThreadFunction;
  rec^.Param:=p;
  if stacksize=0 then
    stacksize:=DefaultStackSize;
  rec^.StackLen:=stacksize;
  rec^.Started:=ctr_alloc_event(1,0);
  { core -100: the RTL default core (see fpcctr_set_default_core) }
  rec^.Handle:=ctr_thread_create(@CtrThreadMain,rec,stacksize,0,-100);
  if rec^.Handle=nil then
    begin
      ctr_free(rec^.Started);
      ctr_cfree(rec);
      exit;
    end;
  ThreadId:=TThreadID(rec);
  CtrBeginThread:=TThreadID(rec);
  ctr_event_set(rec^.Started);
end;

procedure CtrEndThread(ExitCode: DWord);
var
  rec: PCtrThreadRec;
begin
  rec:=CtrCurrentThread;
  DoneThread;
  CtrReleaseThreadVars;
  if (rec<>nil) and (rec<>@CtrMainThreadRec) then
    begin
      if rec^.SelfClosed then
        begin
          { CloseThread was called from this thread (FreeOnTerminate):
            nobody will join, let libctru free the thread on exit }
          ctr_free(rec^.Started);
          ctr_thread_detach(rec^.Handle);
          ctr_cfree(rec);
        end;
      ctr_thread_exit(LongInt(ExitCode));
    end
  else
    ctr_exit(LongInt(ExitCode));
end;

function CtrSuspendThread(threadHandle: TThreadID): DWord;
begin
  { not supported, TThread implements suspension with events }
  CtrSuspendThread:=DWord(-1);
end;

function CtrResumeThread(threadHandle: TThreadID): DWord;
begin
  CtrResumeThread:=DWord(-1);
end;

function CtrKillThread(threadHandle: TThreadID): DWord;
begin
  CtrKillThread:=DWord(-1);
end;

function CtrCloseThread(threadHandle: TThreadID): DWord;
var
  rec: PCtrThreadRec;
begin
  CtrCloseThread:=0;
  rec:=PCtrThreadRec(threadHandle);
  if (rec=nil) or (rec=@CtrMainThreadRec) then
    exit;
  if rec=CtrCurrentThread then
    begin
      { freed by CtrEndThread }
      rec^.SelfClosed:=true;
      exit;
    end;
  ctr_thread_join(rec^.Handle,-1);
  ctr_thread_free(rec^.Handle);
  ctr_free(rec^.Started);
  ctr_cfree(rec);
end;

procedure CtrThreadSwitch;
begin
  ctr_yield;
end;

function CtrWaitForThreadTerminate(threadHandle: TThreadID; TimeoutMs: LongInt): DWord;
var
  rec: PCtrThreadRec;
  timeout: Int64;
begin
  rec:=PCtrThreadRec(threadHandle);
  if (rec=nil) or (rec=@CtrMainThreadRec) then
    begin
      CtrWaitForThreadTerminate:=DWord(-1);
      exit;
    end;
  { 0 = no timeout }
  if TimeoutMs<=0 then
    timeout:=-1
  else
    timeout:=Int64(TimeoutMs)*1000000;
  if ctr_thread_join(rec^.Handle,timeout)=0 then
    CtrWaitForThreadTerminate:=DWord(ctr_thread_exitcode(rec^.Handle))
  else
    CtrWaitForThreadTerminate:=DWord(-1);
end;

function CtrThreadSetPriority(threadHandle: TThreadID; Prio: LongInt): Boolean;
var
  rec: PCtrThreadRec;
  h: Pointer;
begin
  rec:=PCtrThreadRec(threadHandle);
  if (rec=nil) or (rec=@CtrMainThreadRec) then
    h:=nil
  else
    h:=rec^.Handle;
  if (rec<>nil) and (rec<>CtrCurrentThread) and (h=nil) then
    exit(false);
  { one kernel priority step per 2 Pascal steps, higher Pascal = higher }
  CtrThreadSetPriority:=ctr_thread_set_priority(h,-(Prio div 2))<>0;
end;

function CtrThreadGetPriority(threadHandle: TThreadID): LongInt;
var
  rec: PCtrThreadRec;
begin
  rec:=PCtrThreadRec(threadHandle);
  if (rec=nil) or (rec=@CtrMainThreadRec) then
    CtrThreadGetPriority:=-2*ctr_thread_get_priority(nil)
  else
    CtrThreadGetPriority:=-2*ctr_thread_get_priority(rec^.Handle);
end;

function CtrGetCurrentThreadId: TThreadID;
begin
  CtrGetCurrentThreadId:=TThreadID(CtrCurrentThread);
  if CtrGetCurrentThreadId=0 then
    CtrGetCurrentThreadId:=TThreadID(ctr_thread_self);
end;

procedure CtrSetThreadDebugNameA(threadHandle: TThreadID; const ThreadName: AnsiString);
begin
end;

{$ifdef FPC_HAS_FEATURE_UNICODESTRINGS}
procedure CtrSetThreadDebugNameU(threadHandle: TThreadID; const ThreadName: UnicodeString);
begin
end;
{$endif FPC_HAS_FEATURE_UNICODESTRINGS}

{*****************************************************************************
                          Critical sections
*****************************************************************************}

procedure CtrInitCriticalSection(var cs);
begin
  ctr_lock_init(@cs);
end;

procedure CtrDoneCriticalSection(var cs);
begin
end;

procedure CtrEnterCriticalSection(var cs);
begin
  ctr_lock_enter(@cs);
end;

function CtrTryEnterCriticalSection(var cs): LongInt;
begin
  CtrTryEnterCriticalSection:=ctr_lock_tryenter(@cs);
end;

procedure CtrLeaveCriticalSection(var cs);
begin
  ctr_lock_leave(@cs);
end;

{*****************************************************************************
                                Events
*****************************************************************************}

function CtrBasicEventCreate(EventAttributes: Pointer; AManualReset, InitialState: Boolean; const Name: AnsiString): PEventState;
begin
  CtrBasicEventCreate:=PEventState(ctr_alloc_event(ord(AManualReset),ord(InitialState)));
end;

procedure CtrBasicEventDestroy(state: PEventState);
begin
  ctr_free(state);
end;

procedure CtrBasicEventResetEvent(state: PEventState);
begin
  ctr_event_reset(state);
end;

procedure CtrBasicEventSetEvent(state: PEventState);
begin
  ctr_event_set(state);
end;

{ wrSignaled = 0, wrTimeout = 1, wrAbandoned = 2, wrError = 3 }
function CtrBasicEventWaitFor(timeout: Cardinal; state: PEventState; FUseComWait: Boolean=False): LongInt;
var
  ms: LongInt;
begin
  if timeout=$FFFFFFFF then
    ms:=-1
  else if timeout>$7FFFFFFF then
    ms:=$7FFFFFFF
  else
    ms:=LongInt(timeout);
  if ctr_event_wait_timeout(state,ms)<>0 then
    CtrBasicEventWaitFor:=0
  else
    CtrBasicEventWaitFor:=1;
end;

function CtrRTLEventCreate: PRTLEvent;
begin
  { auto-reset, initially not signalled }
  CtrRTLEventCreate:=PRTLEvent(ctr_alloc_event(0,0));
end;

procedure CtrRTLEventDestroy(AEvent: PRTLEvent);
begin
  ctr_free(AEvent);
end;

procedure CtrRTLEventSetEvent(AEvent: PRTLEvent);
begin
  ctr_event_set(AEvent);
end;

procedure CtrRTLEventResetEvent(AEvent: PRTLEvent);
begin
  ctr_event_reset(AEvent);
end;

procedure CtrRTLEventWaitFor(AEvent: PRTLEvent);
begin
  ctr_event_wait(AEvent);
end;

procedure CtrRTLEventWaitForTimeout(AEvent: PRTLEvent; timeout: LongInt);
begin
  ctr_event_wait_timeout(AEvent,timeout);
end;

{*****************************************************************************
                             Initialization
*****************************************************************************}

function CtrInitManager: Boolean;
begin
  CtrInitManager:=true;
end;

function CtrDoneManager: Boolean;
begin
  CtrDoneManager:=true;
end;

{$define HAS_GETCPUCOUNT}
function GetCPUCount: LongWord;
begin
  GetCPUCount:=ctr_cpu_count;
end;

procedure InitSystemThreads;
var
  tm: TThreadManager;
begin
  with tm do
    begin
      InitManager            :=@CtrInitManager;
      DoneManager            :=@CtrDoneManager;
      BeginThread            :=@CtrBeginThread;
      EndThread              :=@CtrEndThread;
      SuspendThread          :=@CtrSuspendThread;
      ResumeThread           :=@CtrResumeThread;
      KillThread             :=@CtrKillThread;
      CloseThread            :=@CtrCloseThread;
      ThreadSwitch           :=@CtrThreadSwitch;
      WaitForThreadTerminate :=@CtrWaitForThreadTerminate;
      ThreadSetPriority      :=@CtrThreadSetPriority;
      ThreadGetPriority      :=@CtrThreadGetPriority;
      GetCurrentThreadId     :=@CtrGetCurrentThreadId;
      SetThreadDebugNameA    :=@CtrSetThreadDebugNameA;
{$ifdef FPC_HAS_FEATURE_UNICODESTRINGS}
      SetThreadDebugNameU    :=@CtrSetThreadDebugNameU;
{$endif FPC_HAS_FEATURE_UNICODESTRINGS}
      InitCriticalSection    :=@CtrInitCriticalSection;
      DoneCriticalSection    :=@CtrDoneCriticalSection;
      EnterCriticalSection   :=@CtrEnterCriticalSection;
      TryEnterCriticalSection:=@CtrTryEnterCriticalSection;
      LeaveCriticalSection   :=@CtrLeaveCriticalSection;
      InitThreadVar          :=@CtrInitThreadVar;
      RelocateThreadVar      :=@CtrRelocateThreadVar;
      AllocateThreadVars     :=@CtrAllocateThreadVars;
      ReleaseThreadVars      :=@CtrReleaseThreadVars;
      BasicEventCreate       :=@CtrBasicEventCreate;
      BasicEventDestroy      :=@CtrBasicEventDestroy;
      BasicEventResetEvent   :=@CtrBasicEventResetEvent;
      BasicEventSetEvent     :=@CtrBasicEventSetEvent;
      BasiceventWaitFor      :=@CtrBasicEventWaitFor;
      RTLEventCreate         :=@CtrRTLEventCreate;
      RTLEventDestroy        :=@CtrRTLEventDestroy;
      RTLEventSetEvent       :=@CtrRTLEventSetEvent;
      RTLEventResetEvent     :=@CtrRTLEventResetEvent;
      RTLEventWaitFor        :=@CtrRTLEventWaitFor;
      RTLEventWaitForTimeout :=@CtrRTLEventWaitForTimeout;
    end;
  SetThreadManager(tm);
  { allocate and copy the threadvars of the main thread }
  InitThreadVars(@CtrRelocateThreadVar);
  CtrCurrentThread:=@CtrMainThreadRec;
  ThreadID:=TThreadID(@CtrMainThreadRec);
end;
