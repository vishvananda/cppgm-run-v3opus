struct pair { char inner[1][2]; };

const pair held = {{"h"}};

int rows()
{
  static const char nested[2][3] = {"ab", "cd"};
  const char here[1][2] = {"e"};
  static const char whole[3] = "fg";
  return nested[1][1] + here[0][0] + whole[1] + held.inner[0][0];
}

int main()
{
  return rows() == 'd' + 'e' + 'g' + 'h' ? 0 : 1;
}
