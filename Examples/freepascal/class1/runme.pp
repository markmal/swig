program runme;

//{$linklib libexample.so}

uses example;

var
  s:Simple;
  
begin

  WriteLn( 'Creating some objects:' );

  s := Simple.Create;

  WriteLn( '    Created Simple ' );

// ----- Member data access -----

// Notice how we can do this using functions specific to
// the 'Simple' class.
  s.x := 20;
  s.y := 30;

  WriteLn( 'Here is current values:' );
  WriteLn( '    Simple = (' , s.x, ' ', s.y, ')' );

// ----- Call move methods -----

  s.move(5.5, 2.2);

  WriteLn( 'Here is current values:' );
  WriteLn( '    Simple = (' , s.x, ' ', s.y, ')' );

// ----- Delete everything -----

  s.Free;

  WriteLn( 'Goodbye' );

end.