  int these_violent_delights(x, y)
    int x;
//      ^ definition local 0
    int y;
//      ^ definition local 1
  {
    return x + y;
//         ^ reference local 0
//             ^ reference local 1
  }
