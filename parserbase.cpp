/**************************************************************************
**                             parserbase.cpp
**                             --------------
**      begin                   : Sun Aug  4 15:05:35 2002
**      Copyright 2002-2004,2009 Otto Bruggeman <bruggie@gmail.com>
**      Copyright 2007,2010 Kevin Kofler   <kevin.kofler@chello.at>
***************************************************************************/
/***************************************************************************
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   ( at your option ) any later version.
**
***************************************************************************/

#include "parserbase.h"

#include <QtCore/QObject>

#include <QLoggingCategory>

#include "diffmodel.h"
#include "diffhunk.h"
#include "difference.h"
#include "komparemodellist.h"

Q_DECLARE_LOGGING_CATEGORY(LIBKOMPAREDIFF2)

using namespace Diff2;

ParserBase::ParserBase( const KompareModelList* list, const QStringList& diff ) :
    m_diffLines( diff ),
    m_currentModel( 0 ),
    m_models( 0 ),
    m_diffIterator( m_diffLines.begin() ),
    m_singleFileDiff( false ),
    m_malformed( false ),
    m_list( list )
{
//	qCDebug(LIBKOMPAREDIFF2) << diff << endl;
//	qCDebug(LIBKOMPAREDIFF2) << m_diffLines << endl;
	m_models = new DiffModelList();

	// used in contexthunkheader
	m_contextHunkHeader1.setPattern( "\\*{15} ?(.*)\\n" ); // capture is for function name
	m_contextHunkHeader2.setPattern( "\\*\\*\\* ([0-9]+),([0-9]+) \\*\\*\\*\\*\\n" );
	// used in contexthunkbody
	m_contextHunkHeader3.setPattern( "--- ([0-9]+),([0-9]+) ----\\n" );

	m_contextHunkBodyRemoved.setPattern( "- (.*)" );
	m_contextHunkBodyAdded.setPattern  ( "\\+ (.*)" );
	m_contextHunkBodyChanged.setPattern( "! (.*)" );
	m_contextHunkBodyContext.setPattern( "  (.*)" );
	m_contextHunkBodyLine.setPattern   ( "[-\\+! ] (.*)" );

	// This regexp sucks... i'll see what happens
	m_normalDiffHeader.setPattern( "diff (?:(?:-|--)[a-zA-Z0-9=\\\"]+ )*(?:|-- +)(.*) +(.*)\\n" );

	m_normalHunkHeaderAdded.setPattern  ( "([0-9]+)a([0-9]+)(|,[0-9]+)(.*)\\n" );
	m_normalHunkHeaderRemoved.setPattern( "([0-9]+)(|,[0-9]+)d([0-9]+)(.*)\\n" );
	m_normalHunkHeaderChanged.setPattern( "([0-9]+)(|,[0-9]+)c([0-9]+)(|,[0-9]+)(.*)\\n" );

	m_normalHunkBodyRemoved.setPattern  ( "< (.*)" );
	m_normalHunkBodyAdded.setPattern    ( "> (.*)" );
	m_normalHunkBodyDivider.setPattern  ( "---" );

	m_unifiedDiffHeader1.setPattern    ( "--- ([^\\t]+)(?:\\t([^\\t]+)(?:\\t?)(.*))?\\n" );
	m_unifiedDiffHeader2.setPattern    ( "\\+\\+\\+ ([^\\t]+)(?:\\t([^\\t]+)(?:\\t?)(.*))?\\n" );
	m_unifiedHunkHeader.setPattern     ( "@@ -([0-9]+)(|,([0-9]+)) \\+([0-9]+)(|,([0-9]+)) @@(?: ?)(.*)\\n" );
	m_unifiedHunkBodyAdded.setPattern  ( "\\+(.*)" );
	m_unifiedHunkBodyRemoved.setPattern( "-(.*)" );
	m_unifiedHunkBodyContext.setPattern( " (.*)" );
	m_unifiedHunkBodyLine.setPattern   ( "([-+ ])(.*)" );
}

ParserBase::~ParserBase()
{
	if ( m_models )
		m_models = 0; // do not delete this, i pass it around...
}

enum Kompare::Format ParserBase::determineFormat()
{
	// Write your own format detection routine damn it :)
	return Kompare::UnknownFormat;
}

DiffModelList* ParserBase::parse( bool* malformed )
{
	DiffModelList* result;
	switch( determineFormat() )
	{
		case Kompare::Context :
			result = parseContext();
			break;
		case Kompare::Ed :
			result = parseEd();
			break;
		case Kompare::Normal :
			result = parseNormal();
			break;
		case Kompare::RCS :
			result = parseRCS();
			break;
		case Kompare::Unified :
			result = parseUnified();
			break;
		default: // Unknown and SideBySide for now
			result = 0;
			break;
	}

	// *malformed is set to true if some hunks or parts of hunks were
	// probably missed due to a malformed diff
	if ( malformed )
		*malformed = m_malformed;

	return result;
}

bool ParserBase::parseContextDiffHeader()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseContextDiffHeader()" << endl;
	bool result = false;

	while ( m_diffIterator != m_diffLines.end() )
	{
		if ( !m_contextDiffHeader1.exactMatch( *(m_diffIterator)++ ) )
		{
			continue;
		}
//		qCDebug(LIBKOMPAREDIFF2) << "Matched length Header1 = " << m_contextDiffHeader1.matchedLength() << endl;
//		qCDebug(LIBKOMPAREDIFF2) << "Matched string Header1 = " << m_contextDiffHeader1.cap( 0 ) << endl;
		if ( m_diffIterator != m_diffLines.end() && m_contextDiffHeader2.exactMatch( *m_diffIterator ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Matched length Header2 = " << m_contextDiffHeader2.matchedLength() << endl;
//			qCDebug(LIBKOMPAREDIFF2) << "Matched string Header2 = " << m_contextDiffHeader2.cap( 0 ) << endl;

			m_currentModel = new DiffModel( m_contextDiffHeader1.cap( 1 ), m_contextDiffHeader2.cap( 1 ) );
			m_currentModel->setSourceTimestamp     ( m_contextDiffHeader1.cap( 2 ) );
			m_currentModel->setSourceRevision      ( m_contextDiffHeader1.cap( 4 ) );
			m_currentModel->setDestinationTimestamp( m_contextDiffHeader2.cap( 2 ) );
			m_currentModel->setDestinationRevision ( m_contextDiffHeader2.cap( 4 ) );

			++m_diffIterator;
			result = true;

			break;
		}
		else
		{
			// We're screwed, second line does not match or is not there...
			break;
		}
		// Do not inc the Iterator because the second line might be the first line of
		// the context header and the first hit was a fluke (impossible imo)
		// maybe we should return false here because the diff is broken ?
	}

	return result;
}

bool ParserBase::parseEdDiffHeader()
{
	return false;
}

bool ParserBase::parseNormalDiffHeader()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseNormalDiffHeader()" << endl;
	bool result = false;

	while ( m_diffIterator != m_diffLines.end() )
	{
		if ( m_normalDiffHeader.exactMatch( *m_diffIterator ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Matched length Header = " << m_normalDiffHeader.matchedLength() << endl;
//			qCDebug(LIBKOMPAREDIFF2) << "Matched string Header = " << m_normalDiffHeader.cap( 0 ) << endl;

			m_currentModel = new DiffModel();
			m_currentModel->setSourceFile          ( m_normalDiffHeader.cap( 1 ) );
			m_currentModel->setDestinationFile     ( m_normalDiffHeader.cap( 2 ) );

			result = true;

			++m_diffIterator;
			break;
		}
		else
		{
			qCDebug(LIBKOMPAREDIFF2) << "No match for: " << ( *m_diffIterator ) << endl;
		}
		++m_diffIterator;
	}

	if ( result == false )
	{
		// Set this to the first line again and hope it is a single file diff
		m_diffIterator = m_diffLines.begin();
		m_currentModel = new DiffModel();
		m_singleFileDiff = true;
	}

	return result;
}

bool ParserBase::parseRCSDiffHeader()
{
	return false;
}

bool ParserBase::parseUnifiedDiffHeader()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseUnifiedDiffHeader()" << endl;
	bool result = false;

	while ( m_diffIterator != m_diffLines.end() ) // do not assume we start with the diffheader1 line
	{
		if ( !m_unifiedDiffHeader1.exactMatch( *m_diffIterator ) )
		{
			++m_diffIterator;
			continue;
		}
//		qCDebug(LIBKOMPAREDIFF2) << "Matched length Header1 = " << m_unifiedDiffHeader1.matchedLength() << endl;
//		qCDebug(LIBKOMPAREDIFF2) << "Matched string Header1 = " << m_unifiedDiffHeader1.cap( 0 ) << endl;
		++m_diffIterator;
		if ( m_diffIterator != m_diffLines.end() && m_unifiedDiffHeader2.exactMatch( *m_diffIterator ) )
		{
			m_currentModel = new DiffModel( m_unifiedDiffHeader1.cap( 1 ), m_unifiedDiffHeader2.cap( 1 ) );
			m_currentModel->setSourceTimestamp( m_unifiedDiffHeader1.cap( 2 ) );
			m_currentModel->setSourceRevision( m_unifiedDiffHeader1.cap( 4 ) );
			m_currentModel->setDestinationTimestamp( m_unifiedDiffHeader2.cap( 2 ) );
			m_currentModel->setDestinationRevision( m_unifiedDiffHeader2.cap( 4 ) );

			++m_diffIterator;
			result = true;

			break;
		}
		else
		{
			// We're screwed, second line does not match or is not there...
			break;
		}
	}

	return result;
}

bool ParserBase::parseContextHunkHeader()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseContextHunkHeader()" << endl;

	if ( m_diffIterator == m_diffLines.end() )
		return false;

	if ( !m_contextHunkHeader1.exactMatch( *(m_diffIterator) ) )
		return false; // big fat trouble, aborting...

	++m_diffIterator;

	if ( m_diffIterator == m_diffLines.end() )
		return false;

	if ( !m_contextHunkHeader2.exactMatch( *(m_diffIterator) ) )
		return false; // big fat trouble, aborting...

	++m_diffIterator;

	return true;
}

bool ParserBase::parseEdHunkHeader()
{
	return false;
}

bool ParserBase::parseNormalHunkHeader()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseNormalHunkHeader()" << endl;
	if ( m_diffIterator != m_diffLines.end() )
	{
//		qCDebug(LIBKOMPAREDIFF2) << "Header = " << *m_diffIterator << endl;
		if ( m_normalHunkHeaderAdded.exactMatch( *m_diffIterator ) )
		{
			m_normalDiffType = Difference::Insert;
		}
		else if ( m_normalHunkHeaderRemoved.exactMatch( *m_diffIterator ) )
		{
			m_normalDiffType = Difference::Delete;
		}
		else if ( m_normalHunkHeaderChanged.exactMatch( *m_diffIterator ) )
		{
			m_normalDiffType = Difference::Change;
		}
		else
			return false;

		++m_diffIterator;
		return true;
	}

	return false;
}

bool ParserBase::parseRCSHunkHeader()
{
	return false;
}

bool ParserBase::parseUnifiedHunkHeader()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseUnifiedHunkHeader()" << endl;

	if ( m_diffIterator != m_diffLines.end() && m_unifiedHunkHeader.exactMatch( *m_diffIterator ) )
	{
		++m_diffIterator;
		return true;
	}
	else
	{
//		qCDebug(LIBKOMPAREDIFF2) << "This is not a unified hunk header : " << (*m_diffIterator) << endl;
		return false;
	}

}

bool ParserBase::parseContextHunkBody()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseContextHunkBody()" << endl;

	// Storing the src part of the hunk for later use
	QStringList oldLines;
	for( ; m_diffIterator != m_diffLines.end() && m_contextHunkBodyLine.exactMatch( *m_diffIterator ); ++m_diffIterator ) {
//		qCDebug(LIBKOMPAREDIFF2) << "Added old line: " << *m_diffIterator << endl;
		oldLines.append( *m_diffIterator );
	}

	if( !m_contextHunkHeader3.exactMatch( *m_diffIterator ) )
		return false;

	++m_diffIterator;

	// Storing the dest part of the hunk for later use
	QStringList newLines;
	for( ; m_diffIterator != m_diffLines.end() && m_contextHunkBodyLine.exactMatch( *m_diffIterator ); ++m_diffIterator ) {
//		qCDebug(LIBKOMPAREDIFF2) << "Added new line: " << *m_diffIterator << endl;
		newLines.append( *m_diffIterator );
	}

	QString function = m_contextHunkHeader1.cap( 1 );
//	qCDebug(LIBKOMPAREDIFF2) << "Captured function: " << function << endl;
	int linenoA      = m_contextHunkHeader2.cap( 1 ).toInt();
//	qCDebug(LIBKOMPAREDIFF2) << "Source line number: " << linenoA << endl;
	int linenoB      = m_contextHunkHeader3.cap( 1 ).toInt();
//	qCDebug(LIBKOMPAREDIFF2) << "Dest   line number: " << linenoB << endl;

	DiffHunk* hunk = new DiffHunk( linenoA, linenoB, function );

	m_currentModel->addHunk( hunk );

	QStringList::Iterator oldIt = oldLines.begin();
	QStringList::Iterator newIt = newLines.begin();

	Difference* diff;
	while( oldIt != oldLines.end() || newIt != newLines.end() )
	{
		if( oldIt != oldLines.end() && m_contextHunkBodyRemoved.exactMatch( *oldIt ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Delete: " << endl;
			diff = new Difference( linenoA, linenoB );
			diff->setType( Difference::Delete );
			m_currentModel->addDiff( diff );
//			qCDebug(LIBKOMPAREDIFF2) << "Difference added" << endl;
			hunk->add( diff );
			for( ; oldIt != oldLines.end() && m_contextHunkBodyRemoved.exactMatch( *oldIt ); ++oldIt )
			{
//				qCDebug(LIBKOMPAREDIFF2) << " " << m_contextHunkBodyRemoved.cap( 1 ) << endl;
				diff->addSourceLine( m_contextHunkBodyRemoved.cap( 1 ) );
				linenoA++;
			}
		}
		else if( newIt != newLines.end() && m_contextHunkBodyAdded.exactMatch( *newIt ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Insert: " << endl;
			diff = new Difference( linenoA, linenoB );
			diff->setType( Difference::Insert );
			m_currentModel->addDiff( diff );
//			qCDebug(LIBKOMPAREDIFF2) << "Difference added" << endl;
			hunk->add( diff );
			for( ; newIt != newLines.end() && m_contextHunkBodyAdded.exactMatch( *newIt ); ++newIt )
			{
//				qCDebug(LIBKOMPAREDIFF2) << " " << m_contextHunkBodyAdded.cap( 1 ) << endl;
				diff->addDestinationLine( m_contextHunkBodyAdded.cap( 1 ) );
				linenoB++;
			}
		}
		else if(  ( oldIt == oldLines.end() || m_contextHunkBodyContext.exactMatch( *oldIt ) ) &&
		          ( newIt == newLines.end() || m_contextHunkBodyContext.exactMatch( *newIt ) ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Unchanged: " << endl;
			diff = new Difference( linenoA, linenoB );
			// Do not add this diff with addDiff to the model... no unchanged differences allowed in there...
			diff->setType( Difference::Unchanged );
			hunk->add( diff );
			while( ( oldIt == oldLines.end() || m_contextHunkBodyContext.exactMatch( *oldIt ) ) &&
			       ( newIt == newLines.end() || m_contextHunkBodyContext.exactMatch( *newIt ) ) &&
			       ( oldIt != oldLines.end() || newIt != newLines.end() ) )
			{
				QString l;
				if( oldIt != oldLines.end() )
				{
					l = m_contextHunkBodyContext.cap( 1 );
//					qCDebug(LIBKOMPAREDIFF2) << "old: " << l << endl;
					++oldIt;
				}
				if( newIt != newLines.end() )
				{
					l = m_contextHunkBodyContext.cap( 1 );
//					qCDebug(LIBKOMPAREDIFF2) << "new: " << l << endl;
					++newIt;
				}
				diff->addSourceLine( l );
				diff->addDestinationLine( l );
				linenoA++;
				linenoB++;
			}
		}
		else if( ( oldIt != oldLines.end() && m_contextHunkBodyChanged.exactMatch( *oldIt ) ) ||
		         ( newIt != newLines.end() && m_contextHunkBodyChanged.exactMatch( *newIt ) ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Changed: " << endl;
			diff = new Difference( linenoA, linenoB );
			diff->setType( Difference::Change );
			m_currentModel->addDiff( diff );
//			qCDebug(LIBKOMPAREDIFF2) << "Difference added" << endl;
			hunk->add( diff );
			while( oldIt != oldLines.end() && m_contextHunkBodyChanged.exactMatch( *oldIt ) )
			{
//				qCDebug(LIBKOMPAREDIFF2) << " " << m_contextHunkBodyChanged.cap( 1 ) << endl;
				diff->addSourceLine( m_contextHunkBodyChanged.cap( 1 ) );
				linenoA++;
				++oldIt;
			}
			while( newIt != newLines.end() && m_contextHunkBodyChanged.exactMatch( *newIt ) )
			{
//				qCDebug(LIBKOMPAREDIFF2) << " " << m_contextHunkBodyChanged.cap( 1 ) << endl;
				diff->addDestinationLine( m_contextHunkBodyChanged.cap( 1 ) );
				linenoB++;
				++newIt;
			}
		}
		else
			return false;
		diff->determineInlineDifferences();
	}

	return true;
}

bool ParserBase::parseEdHunkBody()
{
	return false;
}

bool ParserBase::parseNormalHunkBody()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseNormalHunkBody" << endl;

	QString type;

	int linenoA = 0, linenoB = 0;

	if ( m_normalDiffType == Difference::Insert )
	{
		linenoA = m_normalHunkHeaderAdded.cap( 1 ).toInt();
		linenoB = m_normalHunkHeaderAdded.cap( 2 ).toInt();
	}
	else if ( m_normalDiffType == Difference::Delete )
	{
		linenoA = m_normalHunkHeaderRemoved.cap( 1 ).toInt();
		linenoB = m_normalHunkHeaderRemoved.cap( 3 ).toInt();
	}
	else if ( m_normalDiffType == Difference::Change )
	{
		linenoA = m_normalHunkHeaderChanged.cap( 1 ).toInt();
		linenoB = m_normalHunkHeaderChanged.cap( 3 ).toInt();
	}

	DiffHunk* hunk = new DiffHunk( linenoA, linenoB );
	m_currentModel->addHunk( hunk );
	Difference* diff = new Difference( linenoA, linenoB );
	hunk->add( diff );
	m_currentModel->addDiff( diff );

	diff->setType( m_normalDiffType );

	if ( m_normalDiffType == Difference::Change || m_normalDiffType == Difference::Delete )
		for( ; m_diffIterator != m_diffLines.end() && m_normalHunkBodyRemoved.exactMatch( *m_diffIterator ); ++m_diffIterator )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Line = " << *m_diffIterator << endl;
			diff->addSourceLine( m_normalHunkBodyRemoved.cap( 1 ) );
		}

	if ( m_normalDiffType == Difference::Change )
	{
		if( m_diffIterator != m_diffLines.end() && m_normalHunkBodyDivider.exactMatch( *m_diffIterator ) )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Line = " << *m_diffIterator << endl;
			++m_diffIterator;
		}
		else
			return false;
	}

	if ( m_normalDiffType == Difference::Insert || m_normalDiffType == Difference::Change )
		for( ; m_diffIterator != m_diffLines.end() && m_normalHunkBodyAdded.exactMatch( *m_diffIterator ); ++m_diffIterator )
		{
//			qCDebug(LIBKOMPAREDIFF2) << "Line = " << *m_diffIterator << endl;
			diff->addDestinationLine( m_normalHunkBodyAdded.cap( 1 ) );
		}

	return true;
}

bool ParserBase::parseRCSHunkBody()
{
	return false;
}

bool ParserBase::matchesUnifiedHunkLine( QString line ) const
{
	static const QChar context( ' ' );
	static const QChar added  ( '+' );
	static const QChar removed( '-' );

	QChar first = line[0];

	return ( first == context || first == added || first == removed );
}

bool ParserBase::parseUnifiedHunkBody()
{
//	qCDebug(LIBKOMPAREDIFF2) << "ParserBase::parseUnifiedHunkBody" << endl;

	int linenoA = 0, linenoB = 0;
	bool wasNum;

	// Fetching the stuff we need from the hunkheader regexp that was parsed in parseUnifiedHunkHeader();
	linenoA = m_unifiedHunkHeader.cap( 1 ).toInt();
	int lineCountA = 1, lineCountB = 1; // an ommitted line count in the header implies a line count of 1
	if( !m_unifiedHunkHeader.cap( 3 ).isEmpty() )
	{
		lineCountA = m_unifiedHunkHeader.cap( 3 ).toInt(&wasNum);
		if( !wasNum )
			return false;

		// If a hunk is an insertion or deletion with no context, the line number given
		// is the one before the hunk. this isn't what we want, so increment it to fix this.
		if(lineCountA == 0)
			linenoA++;
	}
	linenoB = m_unifiedHunkHeader.cap( 4 ).toInt();
	if( !m_unifiedHunkHeader.cap( 6 ).isEmpty() ) {
		lineCountB = m_unifiedHunkHeader.cap( 6 ).toInt(&wasNum);
		if( !wasNum )
			return false;

		if(lineCountB == 0) // see above
			linenoB++;
	}
	QString function = m_unifiedHunkHeader.cap( 7 );

	DiffHunk* hunk = new DiffHunk( linenoA, linenoB, function );
	m_currentModel->addHunk( hunk );

	const QStringList::ConstIterator m_diffLinesEnd = m_diffLines.end();

	const QString context = QString( " " );
	const QString added   = QString( "+" );
	const QString removed = QString( "-" );

	while( m_diffIterator != m_diffLinesEnd && matchesUnifiedHunkLine( *m_diffIterator ) && (lineCountA || lineCountB) )
	{
		Difference* diff = new Difference( linenoA, linenoB );
		hunk->add( diff );

		if( (*m_diffIterator).startsWith( context ) )
		{	// context
			for( ; m_diffIterator != m_diffLinesEnd && (*m_diffIterator).startsWith( context ) && (lineCountA || lineCountB); ++m_diffIterator )
			{
				diff->addSourceLine( QString( *m_diffIterator ).remove( 0, 1 ) );
				diff->addDestinationLine( QString( *m_diffIterator ).remove( 0, 1 ) );
				linenoA++;
				linenoB++;
				--lineCountA;
				--lineCountB;
			}
		}
		else
		{	// This is a real difference, not context
			for( ; m_diffIterator != m_diffLinesEnd && (*m_diffIterator).startsWith( removed ) && (lineCountA || lineCountB); ++m_diffIterator )
			{
				diff->addSourceLine( QString( *m_diffIterator ).remove( 0, 1 ) );
				linenoA++;
				--lineCountA;
			}
			for( ; m_diffIterator != m_diffLinesEnd && (*m_diffIterator).startsWith( added ) && (lineCountA || lineCountB); ++m_diffIterator )
			{
				diff->addDestinationLine( QString( *m_diffIterator ).remove( 0, 1 ) );
				linenoB++;
				--lineCountB;
			}
			if ( diff->sourceLineCount() == 0 )
			{
				diff->setType( Difference::Insert );
//				qCDebug(LIBKOMPAREDIFF2) << "Insert difference" << endl;
			}
			else if ( diff->destinationLineCount() == 0 )
			{
				diff->setType( Difference::Delete );
//				qCDebug(LIBKOMPAREDIFF2) << "Delete difference" << endl;
			}
			else
			{
				diff->setType( Difference::Change );
//				qCDebug(LIBKOMPAREDIFF2) << "Change difference" << endl;
			}
			diff->determineInlineDifferences();
			m_currentModel->addDiff( diff );
		}
	}

	return true;
}

void ParserBase::checkHeader( const QRegExp& header )
{
	if ( m_diffIterator != m_diffLines.end()
	     && !header.exactMatch( *m_diffIterator )
	     && !m_diffIterator->startsWith("Index: ") /* SVN diff */
	     && !m_diffIterator->startsWith("diff ") /* concatenated diff */)
		m_malformed = true;
}

DiffModelList* ParserBase::parseContext()
{
	while ( parseContextDiffHeader() )
	{
		while ( parseContextHunkHeader() )
			parseContextHunkBody();
		if ( m_currentModel->differenceCount() > 0 )
			m_models->append( m_currentModel );
		checkHeader( m_contextDiffHeader1 );
	}

	m_models->sort();

	if ( m_models->count() > 0 )
	{
		return m_models;
	}
	else
	{
		delete m_models;
		return 0L;
	}
}

DiffModelList* ParserBase::parseEd()
{
	while ( parseEdDiffHeader() )
	{
		while ( parseEdHunkHeader() )
			parseEdHunkBody();
		if ( m_currentModel->differenceCount() > 0 )
			m_models->append( m_currentModel );
	}

	m_models->sort();

	if ( m_models->count() > 0 )
	{
		return m_models;
	}
	else
	{
		delete m_models;
		return 0L;
	}
}

DiffModelList* ParserBase::parseNormal()
{
	while ( parseNormalDiffHeader() )
	{
		while ( parseNormalHunkHeader() )
			parseNormalHunkBody();
		if ( m_currentModel->differenceCount() > 0 )
			m_models->append( m_currentModel );
		checkHeader( m_normalDiffHeader );
	}

	if ( m_singleFileDiff )
	{
		while ( parseNormalHunkHeader() )
			parseNormalHunkBody();
		if ( m_currentModel->differenceCount() > 0 )
			m_models->append( m_currentModel );
		if ( m_diffIterator != m_diffLines.end() )
			m_malformed = true;
	}

	m_models->sort();

	if ( m_models->count() > 0 )
	{
		return m_models;
	}
	else
	{
		delete m_models;
		return 0L;
	}
}

DiffModelList* ParserBase::parseRCS()
{
	while ( parseRCSDiffHeader() )
	{
		while ( parseRCSHunkHeader() )
			parseRCSHunkBody();
		if ( m_currentModel->differenceCount() > 0 )
			m_models->append( m_currentModel );
	}

	m_models->sort();

	if ( m_models->count() > 0 )
	{
		return m_models;
	}
	else
	{
		delete m_models;
		return 0L;
	}
}

DiffModelList* ParserBase::parseUnified()
{
	while ( parseUnifiedDiffHeader() )
	{
		while ( parseUnifiedHunkHeader() )
			parseUnifiedHunkBody();
//		qCDebug(LIBKOMPAREDIFF2) << "New model ready to be analyzed..." << endl;
//		qCDebug(LIBKOMPAREDIFF2) << " differenceCount() == " << m_currentModel->differenceCount() << endl;
		if ( m_currentModel->differenceCount() > 0 )
			m_models->append( m_currentModel );
		checkHeader( m_unifiedDiffHeader1 );
	}

	m_models->sort();

	if ( m_models->count() > 0 )
	{
		return m_models;
	}
	else
	{
		delete m_models;
		return 0L;
	}
}

