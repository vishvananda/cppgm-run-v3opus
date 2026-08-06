// N3485 focus: 9.4.2 [class.static.data], 3.3.2 [basic.scope.pdecl]
// A definition written with a nested-name-specifier defines the object the
// region it names already declares, so it declares nothing there: its line
// stands where the definition is written, spelled the way it wrote it, and the
// class keeps the one line its own declaration wrote.
struct holder
{
  static int shared;
};
namespace space
{
  struct nested
  {
    static int counted;
  };
}
int holder::shared;
int space::nested::counted;
