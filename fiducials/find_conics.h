/* This file is part of the fiducials Project.
 * https://github.com/stevenlovegrove/fiducials
 *
 * Copyright (c) 2011 Steven Lovegrove
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef FIND_CONICS_H
#define FIND_CONICS_H

#include <TooN/TooN.h>
#include <TooN/LU.h>
#include <TooN/SVD.h>

#include <fiducials/label.h>

template<typename TdI>
TooN::Matrix<3,3> FindEllipse(
  const CVD::BasicImage<TdI>& dI,
  const IRectangle& r,
  double& residual
) {
  //Precise ellipse estimation without contour point extraction
  //Jean-Nicolas Ouellet, Patrick Hebert

  const TooN::Vector<2> c = TooN::makeVector(
    r.x1 + (r.x2-r.x1) / 2.0,
    r.y1 + (r.y2-r.y1) / 2.0
  );

  const TooN::Vector<2> f = TooN::makeVector(
    2.0 / r.Width(), 2.0 / r.Height()
  );

  // Transform to approximately unit circle at origin
  const TooN::Matrix<3,3> H = TooN::Data(
      f[0], 0, c[0],
      0, f[1], c[1],
      0, 0, 1
  );

  const TooN::Matrix<3,3> Hinv = TooN::Data(
      1/f[0], 0, -c[0]/f[0],
      0, 1/f[1], -c[1]/f[1],
      0, 0, 1
  );

  // Form system Ax = b to solve
  TooN::Matrix<5,5> A = TooN::Zeros;
  TooN::Vector<5> b = TooN::Zeros;

  float elementCount = 0;
  for( int v=r.y1; v<=r.y2; ++v )
  {
    for( int u=r.x1; u<=r.x2; ++u )
    {
      // li = (ai,bi,ci)' = (I_ui,I_vi, -dI' x_i)'
      const TooN::Vector<3> d =
        TooN::makeVector(dI[v][u][0],dI[v][u][1],-(dI[v][u][0] * u + dI[v][u][1] * v) );
      const TooN::Vector<3> li = //H.T() * d;
        TooN::makeVector( d[0]*H(0,0), d[1]*H(1,1), d[0]*H(0,2) + d[1] * H(1,2) + d[2] );
      const TooN::Vector<5> Ki = TooN::makeVector(li[0]*li[0],li[0]*li[1],li[1]*li[1],li[0]*li[2],li[1]*li[2]);
      A += Ki.as_col()*Ki.as_row();
      b += -Ki*li[2]*li[2];
      elementCount++;
    }
  }

  TooN::SVD<> svd(A);
  const TooN::Vector<5> x = svd.backsub(b);

//  //compute the risidual on the system to see if the algebraic error is too large.
//  //note: maybe there is a better error metric.
//  const TooN::Vector<5> error =  A*x - b;
//  residual = error*error;
//  //residual/=elementCount;

  const TooN::Matrix<3,3> C_star_norm = TooN::Data(x[0],x[1]/2.0,x[3]/2.0,  x[1]/2.0,x[2],x[4]/2.0,  x[3]/2.0,x[4]/2.0,1.0);
  const TooN::Matrix<3,3> C = Hinv.T() * TooN::LU<3>(C_star_norm).get_inverse() * Hinv;
//  const Matrix<3,3> C_star = LU<3>(C).get_inverse();
//  return C_star/C_star[2][2];

  return C/C[2][2];
}

void FindCandidateConicsFromLabels(
  CVD::ImageRef size,
  const std::vector<PixelClass>& labels,
  std::vector<PixelClass>& candidates,
  float min_area,
  float max_area,
  float min_density,
  float min_aspect
) {
  const int border = 3;

  for( unsigned int i=0; i<labels.size(); ++i )
  {
    if( labels[i].equiv == -1 )
    {
      const IRectangle& r = labels[i].bbox;
      // reject rectangles clipped by camera view
      if( r.x1 >= border && r.y1 >= border && r.x2 < size.x-border && r.y2 < size.y-border)
      {
        const int area = r.Width() * r.Height();
        if( min_area <= area && area <= max_area )
        {
          const float aspect = (float)r.Width() / (float)r.Height();
          if( min_aspect < aspect && aspect < 1.0 / min_aspect )
          {
            const double density = (double)labels[i].size / (double)area;
            if( min_density <=  density )
            {
              PixelClass candidate = labels[i];
              candidate.bbox = r.Grow(2).Clamp(border,border,size.x-(1+border),size.y-(1+border));
              candidates.push_back(candidate);
            }
          }

        }
      }
    }
  }
}

template<typename TdI>
void FindConics(
    const std::vector<PixelClass>& candidates,
    const CVD::BasicImage<TdI>& dI,
    std::vector<Conic>& conics
) {
  for( unsigned int i=0; i<candidates.size(); ++i )
  {
      const IRectangle region = candidates[i].bbox;

      Conic conic;
      double residual = 0;
      conic.C = FindEllipse(dI,region, residual);

      conic.bbox = region;
      conic.Dual = TooN::LU<3>(conic.C).get_inverse();
      conic.Dual /= conic.Dual[2][2];
      conic.center = TooN::makeVector(conic.Dual[0][2],conic.Dual[1][2]);

      if( region.Contains(conic.center))
        conics.push_back( conic );
  }
}

#endif // FIND_CONICS