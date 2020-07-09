/***************************************************************************
                         qgsrasterdrawer.cpp
                         ---------------------
    begin                : June 2012
    copyright            : (C) 2012 by Radim Blazek
    email                : radim dot blazek at gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgslogger.h"
#include "qgsrasterblock.h"
#include "qgsrasterdrawer.h"
#include "qgsrasterinterface.h"
#include "qgsrasteriterator.h"
#include "qgsrasterviewport.h"
#include "qgsmaptopixel.h"
#include "qgsrendercontext.h"
#include <QImage>
#include <QPainter>
#ifndef QT_NO_PRINTER
#include <QPrinter>
#endif

QgsRasterDrawer::QgsRasterDrawer( QgsRasterIterator *iterator ): mIterator( iterator )
{
}

void QgsRasterDrawer::draw( QPainter *p, QgsRasterViewPort *viewPort, const QgsMapToPixel *qgsMapToPixel, QgsRasterBlockFeedback *feedback )
{
  QgsDebugMsgLevel( QStringLiteral( "Entered" ), 4 );
  if ( !p || !mIterator || !viewPort || !qgsMapToPixel )
  {
    return;
  }

  // last pipe filter has only 1 band
  int bandNumber = 1;
  mIterator->startRasterRead( bandNumber, viewPort->mWidth, viewPort->mHeight, viewPort->mDrawnExtent, feedback );

  //number of cols/rows in output pixels
  int nCols = 0;
  int nRows = 0;
  //shift to top left point for the raster part
  int topLeftCol = 0;
  int topLeftRow = 0;

  // We know that the output data type of last pipe filter is QImage data

  std::unique_ptr< QgsRasterBlock > block;

  // readNextRasterPart calcs and resets  nCols, nRows, topLeftCol, topLeftRow
  while ( mIterator->readNextRasterPart( bandNumber, nCols, nRows,
                                         block, topLeftCol, topLeftRow ) )
  {
    if ( !block )
    {
      QgsDebugMsg( QStringLiteral( "Cannot get block" ) );
      continue;
    }

    QImage img = block->image();

#ifndef QT_NO_PRINTER
    // Because of bug in Acrobat Reader we must use "white" transparent color instead
    // of "black" for PDF. See #9101.
    QPrinter *printer = dynamic_cast<QPrinter *>( p->device() );
    if ( printer && printer->outputFormat() == QPrinter::PdfFormat )
    {
      QgsDebugMsgLevel( QStringLiteral( "PdfFormat" ), 4 );

      img = img.convertToFormat( QImage::Format_ARGB32 );
      QRgb transparentBlack = qRgba( 0, 0, 0, 0 );
      QRgb transparentWhite = qRgba( 255, 255, 255, 0 );
      for ( int x = 0; x < img.width(); x++ )
      {
        for ( int y = 0; y < img.height(); y++ )
        {
          if ( img.pixel( x, y ) == transparentBlack )
          {
            img.setPixel( x, y, transparentWhite );
          }
        }
      }
    }
#endif

    if ( feedback && feedback->renderPartialOutput() )
    {
      // there could have been partial preview written before
      // so overwrite anything with the resulting image.
      // (we are guaranteed to have a temporary image for this layer, see QgsMapRendererJob::needTemporaryImage)
      p->setCompositionMode( QPainter::CompositionMode_Source );
    }

    drawImage( p, viewPort, img, topLeftCol, topLeftRow, qgsMapToPixel );

    if ( feedback && feedback->renderPartialOutput() )
    {
      // go back to the default composition mode
      p->setCompositionMode( QPainter::CompositionMode_SourceOver );
    }

    // OK this does not matter much anyway as the tile size quite big so most of the time
    // there would be just one tile for the whole display area, but it won't hurt...
    if ( feedback && feedback->isCanceled() )
      break;
  }
}

void QgsRasterDrawer::drawImage( QPainter *p, QgsRasterViewPort *viewPort, const QImage &img, int topLeftCol, int topLeftRow, const QgsMapToPixel *qgsMapToPixel ) const
{
  if ( !p || !viewPort )
  {
    return;
  }

  //top left position in device coords
  QPoint tlPoint = QPoint( viewPort->mTopLeftPoint.x() + topLeftCol, viewPort->mTopLeftPoint.y() + topLeftRow );
  p->save();
  p->setRenderHint( QPainter::Antialiasing, false );

  // Blending problem was reported with PDF output if background color has alpha < 255
  // in #7766, it seems to be a bug in Qt, setting a brush with alpha 255 is a workaround
  // which should not harm anything
  p->setBrush( QBrush( QColor( Qt::white ), Qt::NoBrush ) );

  if ( qgsMapToPixel )
  {
    int w = qgsMapToPixel->mapWidth();
    int h = qgsMapToPixel->mapHeight();

    double rotation = qgsMapToPixel->mapRotation();
    if ( rotation )
    {
      // both viewPort and image sizes are dependent on scale
      double cx = w / 2.0;
      double cy = h / 2.0;
      p->translate( cx, cy );
      p->rotate( rotation );
      p->translate( -cx, -cy );
    }
  }

  p->drawImage( tlPoint, img );

#if 0
  // For debugging:
  QRectF br = QRectF( tlPoint, img.size() );
  QPointF c = br.center();
  double rad = std::max( br.width(), br.height() ) / 10;
  p->drawRoundedRect( br, rad, rad );
  p->drawLine( QLineF( br.x(), br.y(), br.x() + br.width(), br.y() + br.height() ) );
  p->drawLine( QLineF( br.x() + br.width(), br.y(), br.x(), br.y() + br.height() ) );

  double nw = br.width() * 0.5;
  double nh = br.height() * 0.5;
  br = QRectF( c - QPointF( nw / 2, nh / 2 ), QSize( nw, nh ) );
  p->drawRoundedRect( br, rad, rad );

  nw = br.width() * 0.5;
  nh = br.height() * 0.5;
  br = QRectF( c - QPointF( nw / 2, nh / 2 ), QSize( nw, nh ) );
  p->drawRoundedRect( br, rad, rad );
#endif

  p->restore();
}

