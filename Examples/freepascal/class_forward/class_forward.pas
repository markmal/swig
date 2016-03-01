(* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 3.0.9
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- *)

unit class_forward;

{$mode objfpc}{$H+}

{$define class_forward_FUNCTION_WRAPPER}
{$define class_forward_CLASS_WRAPPER}

interface



// interface_uses



// interface_type_begin 


//pasraw_intf.f

type
  CPA = pointer;

type
  CPC = pointer;

// interface_type_end 



// interface_functions

  function New_A( ):CPA; stdcall;

  procedure Delete_A( self:CPA ); stdcall;

  function New_C( ):CPC; stdcall;

  procedure Delete_C( self:CPC ); stdcall;

{$ifdef class_forward_FUNCTION_WRAPPER}

// interface_functions_wrapper

{$endif} //class_forward_FUNCTION_WRAPPER

{$ifdef class_forward_CLASS_WRAPPER}

type

  TB = class (TObject)

    private
      FCObjPtr : CPA;
      FOwnCObjPtr : boolean;
    
    protected
      procedure SetCObjPtr(Value : CPA);
    
    public

    constructor Create ();overload; 

    destructor Destroy; override;

  //various other methods
  
  public  
    property CObjPtr : CPA read FCObjPtr write SetCObjPtr;
    property OwnCObjPtr : boolean read FOwnCObjPtr  write FOwnCObjPtr ;
  //proxy class methods
  end; {TB}

  TC = class (TA) 
    public

    constructor Create ();overload; 

    destructor Destroy; override;

  //various other methods
  //proxy class methods
  end; {TC}

{$endif} //class_forward_CLASS_WRAPPER
// Output a Pascal type wrapper class for each SWIG type
implementation


// implementation_type_begin



// implementation_uses

// implementation_type_end

// implementation_functions

{$IFDEF LINUX}
{$linklib libclass_forward.so}
const __WRAPDLLNAME= 'libclass_forward_wrap.so';
{$ENDIF}

{$IFDEF MSWINDOWS}
{$linklib class_forward.dll}
const __WRAPDLLNAME= 'class_forward_wrap.dll';
{$ENDIF}

{$IFDEF HAIKU}
{$linklib libclass_forward.so}
const __WRAPDLLNAME= 'libclass_forward_wrap.so';
{$ENDIF}

{$IFDEF QTOPIA}
{$linklib libclass_forward.so}
const __WRAPDLLNAME= 'libclass_forward_wrap.so';
{$ENDIF}

{$IFDEF DARWIN}
{$linklib class_forward}
const __WRAPDLLNAME= '';
{$LINKFRAMEWORK class_forward}
{$ENDIF}
 


  function New_A( ):CPA; stdcall; external __WRAPDLLNAME name 'new_A';

  procedure Delete_A( self:CPA ); stdcall; external __WRAPDLLNAME name 'delete_A';

  function New_C( ):CPC; stdcall; external __WRAPDLLNAME name 'new_C';

  procedure Delete_C( self:CPC ); stdcall; external __WRAPDLLNAME name 'delete_C';

{$ifdef class_forward_FUNCTION_WRAPPER}

// implementation_functions_wrapper

{$endif} //class_forward_FUNCTION_WRAPPER

{$ifdef class_forward_CLASS_WRAPPER}

constructor TB.Create ();
begin
  inherited Create;
  FOwnCObjPtr := true;
   FCObjPtr := class_forward.New_A();
end;

destructor TB.Destroy; 
begin   
  if (FCObjPtr <> nil) and  FOwnCObjPtr then begin 
    class_forward.delete_A(FCObjPtr);
    FOwnCObjPtr := false;
  end;
  FCObjPtr := nil; 
  inherited Destroy;
end;

procedure TB.SetCObjPtr(Value : CPA);
begin
  if (Value <> FCObjPtr) then begin
  if (FCObjPtr <> nil) and  FOwnCObjPtr then begin 
    class_forward.delete_A(FCObjPtr);
  end;
  FCObjPtr := Value;
  end;
end;


constructor TC.Create ();
begin
  inherited Create;
  FOwnCObjPtr := true;
   FCObjPtr := class_forward.New_C();
end;

destructor TC.Destroy;
begin   
  if (FCObjPtr <> nil) and FOwnCObjPtr then begin 
    class_forward.delete_C(FCObjPtr);
    FOwnCObjPtr := false;
  end;  
  FCObjPtr := nil; 
  inherited Destroy;
end;


{$endif} //class_forward_CLASS_WRAPPER

initialization


// constant_initialization

// initialization


finalization

// finalization


end.
