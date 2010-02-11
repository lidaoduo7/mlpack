#include "fastlib/fastlib.h"

void LoadTimeSeries(const char* filename, Vector* p_time_series) {
  Vector &time_series = *p_time_series;

  Matrix time_series_matrix;
  data::Load(filename, &time_series_matrix);

  int n_dims = time_series_matrix.n_cols();

  time_series.Init(n_dims);
  for(int i = 0; i < n_dims; i++) {
    time_series[i]= time_series_matrix.get(0, i);
  }
}

double ComputeDTWAlignmentScore(const Vector &x, const Vector &y, 
  				ArrayList< GenVector<int> >* p_best_path) {
  ArrayList< GenVector<int> > &best_path = *p_best_path;

  int n_x = x.length();
  int n_y = y.length();

  Matrix gamma;
  gamma.Init(n_x + 1, n_y + 1);

  Matrix best_in;
  best_in.Init(n_x + 1, n_y + 1);

  double dbl_max = std::numeric_limits<double>::max();

  for(int i = 1; i <= n_x; i++) {
    gamma.set(i, 0, dbl_max);
  }
  for(int j = 1; j <= n_y; j++) {
    gamma.set(0, j, dbl_max);
  }

  gamma.set(0, 0, 0);

  for(int i = 1; i <= n_x; i++) {
    for(int j = 1; j <= n_y; j++) {
      double cost = fabs(x[i - 1] - y[j - 1]);

      double c1 = gamma.get(i - 1, j - 1);
      double c2 = gamma.get(i - 1, j);
      double c3 = gamma.get(i, j - 1);
      
      if((c1 <= c2) && (c1 <= c3)) {
	gamma.set(i, j, cost + c1);
	best_in.set(i, j, 0); // best path in is through (i-1, j-1)
      }
      else if((c2 <= c1) && (c2 <= c3)) {
	gamma.set(i, j, cost + c2);
	best_in.set(i, j, -1); // best path in is through (i-1, j)
      }
      else {
	gamma.set(i, j, cost + c3);
	best_in.set(i, j, 1); // best path in is through (i, j-1)
      }
    }
  }

  printf("cost of best path = %f\n", gamma.get(n_x, n_y));

  // reconstruct best path through a trace back
  
  best_path.Init();
  
  int cur_i = n_x;
  int cur_j = n_y;

  int path_length = 0;
  while((cur_i != 0) && (cur_j != 0)) {
    best_path.PushBack(1);
    best_path[path_length].Init(2);

    best_path[path_length][0] = cur_i;
    best_path[path_length][1] = cur_j;

    
    printf("best_path[%d](cur_i, cur_j) = (%d, %d)\n", path_length, cur_i, cur_j);

    path_length++;
    
    double direction_code = best_in.get(cur_i, cur_j);
    if(direction_code < -0.5) {
      cur_i--;
    }
    else if(direction_code > 0.5) {
      cur_j--;
    }
    else {
      cur_i--;
      cur_j--;
    }
  }
  
  
  printf("best path length = %d\n", best_path.size());
  printf("path_length = %d\n", path_length);
  
  printf("n_x + n_y = %d + %d = %d\n", n_x, n_y, n_x + n_y);
  
  printf("Printing best path:\n");

  for(int i = best_path.size() - 1; i >= 0; i--) {
    printf("(%d, %d)\n", best_path[i][0], best_path[i][1]);
  }
  
  return gamma.get(n_x, n_y);
}
