/***************************************************************************
  qgstextblock.cpp
  ---------------
   begin                : May 2020
   copyright            : (C) Nyall Dawson
   email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstextblock.h"
#include "qgstextfragment.h"

QgsTextBlock::QgsTextBlock( const QgsTextFragment &fragment )
{
  mFragments.append( fragment );
}

void QgsTextBlock::append( const QgsTextFragment &fragment )
{
  mFragments.append( fragment );
}

void QgsTextBlock::append( QgsTextFragment &&fragment )
{
  mFragments.push_back( fragment );
}

void QgsTextBlock::clear()
{
  mFragments.clear();
}

bool QgsTextBlock::empty() const
{
  return mFragments.empty();
}

int QgsTextBlock::size() const
{
  return mFragments.size();
}

const QgsTextFragment &QgsTextBlock::at( int index ) const
{
  return mFragments.at( index );
}

QgsTextFragment &QgsTextBlock::operator[]( int index )
{
  return mFragments[ index ];
}

///@cond PRIVATE
QVector< QgsTextFragment >::const_iterator QgsTextBlock::begin() const
{
  return mFragments.begin();
}

QVector< QgsTextFragment >::const_iterator QgsTextBlock::end() const
{
  return mFragments.end();
}
///@endcond
