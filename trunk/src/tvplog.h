/* 
 * File:   tvplog.h
 * Author: ljp
 *
 * Created on March 26, 2011, 4:06 PM
 */

#ifndef TVPLOG_H
#define	TVPLOG_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Always holds a copy of the last log message printed to log
 */
extern char last_logmsg[];


/*
 * Utility funciotn. Formatting version of syslogger
 */
void
_vsyslogf(int priority, char *msg, ...);

/*
 * Utility function
 * Log message to either specified log file or if no file is specified use
 * system logger
 */
void
logmsg(int priority, char *msg, ...) ;



#ifdef	__cplusplus
}
#endif

#endif	/* TVPLOG_H */

