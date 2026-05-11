  int these_violent_delights(x, y)
//^^^ definition [..] `<file>/k&r.c`/
//kind File
//    ^^^^^^^^^^^^^^^^^^^^^^ definition [..] these_violent_delights(9b79fb6aee4c0440).
//    kind Function
    int x;
//      ^ definition local 0
//      kind Parameter
    int y;
//      ^ definition local 1
//      kind Parameter
  {
    return x + y;
//         ^ reference local 0
//             ^ reference local 1
  }
