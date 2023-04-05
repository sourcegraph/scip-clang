  int these_violent_delights(x, y)
//^^^ definition [..] `<file>/k&r.c`/
//    ^^^^^^^^^^^^^^^^^^^^^^ definition [..] these_violent_delights(9b79fb6aee4c0440).
    int x;
//      ^ definition local 0
    int y;
//      ^ definition local 1
  {
    return x + y;
//         ^ reference local 0
//             ^ reference local 1
  }
