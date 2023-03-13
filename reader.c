/* reader.c
 *
 * Chew up a datafile from dir-benchmark. We may even take multiple
 * test runs in. The idea is that we parse the data, but the purpose
 * is (probably) to generate a spreadsheet, graphic, or other.
 *
 * I should probably re-learn AWK... but I find it soothing to write
 * in C (just to keep the brain happy). Actually, given the test program
 * itself is in flux, this keeps me happy by providing validation of the
 * results (against expectations).
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define NOTHING


/* Convert reltime (%ds): or (%dm%ds): into seconds
 */
char *rtime(char *s, int *psc) {
    int cc;
    int n, sc;
 
    cc = sscanf(s, "(%dm%ds)", &n, &sc);
    if (cc != 2 ) {
	cc = sscanf(s, "(%ds)", &sc);
	n = 0;
	if (cc != 1) {
	    fprintf(stderr, "bad reltime conversion: %s\n", s);
	    return NULL;
	}
    }
    sc += n * 60;
    *psc = sc;
    while (*s && (*s != ':')) /* skip to trailing : */
	++s;
    if (*s != ':') {
	fprintf(stderr, "rtime: no term :\n");
	return NULL;
    }
    return s + 1;
}


/* Get integer, with lead, skip to t, t may be '\0' to skip to end of
 * string.
 */
char *geti(char *s, char *l, char t, int *pv) {
    char *ep;
    long lv;

    if (strncmp(s, l, strlen(l)) != 0) {
        fprintf(stderr, "geti: bad lead %s, %s\n", l, s);
	return NULL;
    }
    s += strlen(l);
    while (isblank(*s))
	++s;
    errno = 0;
    lv = strtol(s, &ep, 0);
    if (errno != 0) {
	fprintf(stderr, "geti: strtol: %d error\n", errno);
	return NULL;
    }
    if (s == ep) {
	fprintf(stderr, "geti: strtol: no number\n");
        return NULL;
    }
    s = ep;
    *pv = (int)lv;
    while (*s && (*s != t))
	++s;
    if (*s != t) {
	fprintf(stderr, "geti: no term %c\n", t);
	return NULL;
    }
    return s + 1;
}


/* Get time, with lead, skip to t, t may be '\0' to skip to end of
 * string. Always returns microseconds.
 *
 * Note that microseconds is µs which is utf-8. This is
 * ok, we assume that compiler will accept 8 bit characters.
 * and certainly vim is ok with this, as is gcc.
 *
 * As it is, any of the time variable can be seconds,
 * microseconds or milliseconds. Always return microseconds.
 */
char *gett(char *s, char *l, char t, int *pv) {
    char *ep;
    double x;
    char *su = "µs";

    if (strncmp(s, l, strlen(l)) != 0) {
        fprintf(stderr, "gett: bad lead %s, %s\n", l, s);
	return NULL;
    }
    s += strlen(l);
    while (isblank(*s))
	++s;
    errno = 0;
    x = strtod(s, &ep);
    if (errno != 0) {
	fprintf(stderr, "gett: strtod: %d error\n", errno);
	return NULL;
    }
    if (s == ep) {
	fprintf(stderr, "gett: strtod: no number\n");
        return NULL;
    }
    s = ep;
    /* Convert for units: su is microseconds string. We also accept u;
     * if I have "ascii-normalized" the data. We are assuming large
     * int type -- that is, a single int is able to represent the
     * time in microseconds (32 bit 2s complement int gives 2 billion,
     * which is 2000 million microseconds or 2000 seconds. So, we do
     * the conversion without checks here -- we would notice before
     * the data gets here if there is an extreme).
     */
    if ((*s == su[0]) || (*s == 'u')) {
	*pv = (int)x;
    } else if (*s == 'm') {
	*pv = (int)(x * 1000.0);
    } else if (*s == 's') {
	*pv = (int)(x * 1000000.0);
    } else {
	fprintf(stderr, "gett: scale\n");
	return NULL;
    }
    while (*s && (*s != t))
	++s;
    if (*s != t) {
	fprintf(stderr, "gett: no term %c\n", t);
	return NULL;
    }
    return s + 1;
}


int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    char *cmd = NULL;    /* $ ./dir-benchmark ... */
    char *s, *s2;
    int n;
    char *poolid = NULL; /* pool-9089361637256346193 */
    char *tstamp = NULL; /* hh:mm:ss */
    int r = 1;           /* return, assume error */

    int    cc;   /* conversion count */
    int    hf,   /* highest file */
	   fc,   /* file count */
	   wt,   /* write threads */
	   rt,   /* read threads */
	   dt,   /* delete threads */
           sc,   /* seconds */

           wn,   /* write n */
	   wns,  /* write n/s */
	   wmb,  /* write MB/s */
	   wavg, /* write avg ms */
	   wd,   /* write delay */
	   wo,   /* write open us */
           w,    /* write us */
           wso,  /* write sync-obj ms */
	   wsd,  /* write sync-dir s */
	   wc,   /* write close us */

	   rn,   /* read n */
	   rns,  /* read n/s */
	   rmb,  /* read MB/s */
	   ravg, /* read avg us */
	   ro,   /* read open us */
	   ttfb, /* read ttfb us */
	   rc,   /* read close us */

	   dn,   /* delete n */
	   dns,  /* delete n/s */
	   davg, /* delete avg us */
	   dd;   /* delete delete us */

    /* title line - type is DATA or TOTAL. All times in microseconds.
     */
    printf("type,poolid,tstamp,sc,");
    printf("wn,wns,wmb,wavg,wd,");
    printf("wo,w,wso,wsd,wc,");
    printf("rn,rns,rmb,ravg,ro,");
    printf("ttfb,rc,");
    printf("dn,dns,davg,dd,");
    printf("fc,hf\n");
 
    while ((nread = getline(&line, &len, stdin)) >= 0) {
	if (nread <= 1) {

	    /* Ignore empty lines (just \n)
	     */
	    NOTHING;

	} else if (line[0] == '$') {

	    /* This is the command line that generated the data.
	     * Record it.
	     */
	    free(cmd);
	    cmd = strdup(line);

	} else if (line[0] == '[') {

	    /* Start of pool-id, find end
	     */
	    for (s = line; *s && (*s != ']'); ++s)
		NOTHING;
            if (*s != ']') {
		fprintf(stderr, "No terminating ]: %s\n", line);
		goto err;
	    }
	    *s++ = '\0';
	    free(poolid);
	    poolid = strdup(line + 1);
	    while (isblank(*s))
		++s;
	    if (isdigit(*s)) {
		/* Data line
		 */
		s[8] = '\0'; /* time stamp is 8 chars hh:mm:ss */
		tstamp = strdup(s);
		s += 9;
		s2 = s; /* keep a copy of the line, prior to parsing */
		if ((s = rtime(s, &sc)) == NULL) goto err;

		if ((s = geti(s, " Write: ", '(', &wn)) == NULL) goto err;
		if ((s = geti(s, "", ',', &wns)) == NULL) goto err;
		if ((s = geti(s, " ", ',', &wmb)) == NULL) goto err;
		if ((s = gett(s, " avg ", ',', &wavg)) == NULL) goto err;
		if ((s = gett(s, " delay ", ',', &wd)) == NULL) goto err;
		if ((s = gett(s, " open ", ',', &wo)) == NULL) goto err;
		if ((s = gett(s, " write ", ',', &w)) == NULL) goto err;
		if ((s = gett(s, " sync-obj ", ',', &wso)) == NULL) goto err;
		if ((s = gett(s, " sync-dir ", ',', &wsd)) == NULL) goto err;
		if ((s = gett(s, " close ", ',', &wc)) == NULL) goto err;

		if ((s = geti(s, " Read: ", '(', &rn)) == NULL) goto err;
		if ((s = geti(s, "", ',', &rns)) == NULL) goto err;
		if ((s = geti(s, " ", ',', &rmb)) == NULL) goto err;
		if ((s = gett(s, " avg ", ',', &ravg)) == NULL) goto err;
		if ((s = gett(s, " open ", ',', &ro)) == NULL) goto err;
		if ((s = gett(s, " ttfb ", ',', &ttfb)) == NULL) goto err;
		if ((s = gett(s, " close ", ',', &rc)) == NULL) goto err;

		if ((s = geti(s, " Delete: ", '(', &dn)) == NULL) goto err;
		if ((s = geti(s, "", ',', &dns)) == NULL) goto err;
		if ((s = gett(s, " avg ", ',', &davg)) == NULL) goto err;
		if ((s = gett(s, " delay ", ')', &dd)) == NULL) goto err;
	
		if ((s = geti(s, " fileCount=", ' ', &fc)) == NULL) goto err;
		if ((s = geti(s, "HighestFile=", '\0', &hf)) == NULL) goto err;

		/* printf("%s\n", s2); */
	        printf("DATA,%s,%s,%d,", poolid, tstamp, sc);
		printf("%d,%d,%d,%d,%d,", wn, wns, wmb, wavg, wd);
		printf("%d,%d,%d,%d,%d,", wo, w, wso, wsd, wc);
		printf("%d,%d,%d,%d,%d,", rn, rns, rmb, ravg, ro);
		printf("%d,%d,", ttfb, rc);
		printf("%d,%d,%d,%d,", dn, dns, davg, dd);
		printf("%d,%d\n", fc, hf);

            } else if (strncmp(s, "TOTAL ", 6) == 0) {

		/* TOTAL line -- this data we can use for validation...
		 *
		 * parse this the same way we parse a DATA line
		 */
		s += 6;
		free(tstamp);
		tstamp = NULL;
		while (isblank(*s))
		    ++s;
		/* Note: this is actually identical to DATA -- but, we
		 * don't know enough to merge it (yet).
		 */
		s2 = s; /* keep a copy of the line, prior to parsing */
		if ((s = rtime(s, &sc)) == NULL) goto err;

		if ((s = geti(s, " Write: ", '(', &wn)) == NULL) goto err;
		if ((s = geti(s, "", ',', &wns)) == NULL) goto err;
		if ((s = geti(s, " ", ',', &wmb)) == NULL) goto err;
		if ((s = gett(s, " avg ", ',', &wavg)) == NULL) goto err;
		if ((s = gett(s, " delay ", ',', &wd)) == NULL) goto err;
		if ((s = gett(s, " open ", ',', &wo)) == NULL) goto err;
		if ((s = gett(s, " write ", ',', &w)) == NULL) goto err;
		if ((s = gett(s, " sync-obj ", ',', &wso)) == NULL) goto err;
		if ((s = gett(s, " sync-dir ", ',', &wsd)) == NULL) goto err;
		if ((s = gett(s, " close ", ',', &wc)) == NULL) goto err;

		if ((s = geti(s, " Read: ", '(', &rn)) == NULL) goto err;
		if ((s = geti(s, "", ',', &rns)) == NULL) goto err;
		if ((s = geti(s, " ", ',', &rmb)) == NULL) goto err;
		if ((s = gett(s, " avg ", ',', &ravg)) == NULL) goto err;
		if ((s = gett(s, " open ", ',', &ro)) == NULL) goto err;
		if ((s = gett(s, " ttfb ", ',', &ttfb)) == NULL) goto err;
		if ((s = gett(s, " close ", ',', &rc)) == NULL) goto err;

		if ((s = geti(s, " Delete: ", '(', &dn)) == NULL) goto err;
		if ((s = geti(s, "", ',', &dns)) == NULL) goto err;
		if ((s = gett(s, " avg ", ',', &davg)) == NULL) goto err;
		if ((s = gett(s, " delay ", ')', &dd)) == NULL) goto err;
	
		if ((s = geti(s, " fileCount=", ' ', &fc)) == NULL) goto err;
		if ((s = geti(s, "HighestFile=", '\0', &hf)) == NULL) goto err;

		/* printf("%s\n", s2); */
	        printf("TOTAL,%s,%s,%d,", poolid, tstamp ? tstamp : "", sc);
		printf("%d,%d,%d,%d,%d,", wn, wns, wmb, wavg, wd);
		printf("%d,%d,%d,%d,%d,", wo, w, wso, wsd, wc);
		printf("%d,%d,%d,%d,%d,", rn, rns, rmb, ravg, ro);
		printf("%d,%d,", ttfb, rc);
		printf("%d,%d,%d,%d,", dn, dns, davg, dd);
		printf("%d,%d\n", fc, hf);

	    } else if (strncmp(s, "Highest ", 8) == 0) {

		/* Initial line
		 *
		 * question: why would highest file and file count be
		 *           anything other than zero? read the GO code
		 *           FIXME
		 */
		cc = sscanf(s,
"Highest file = %d, fileCount=%d, Write threads = %d, read threads = %d, delete threads = %d",
	           &hf, &fc, &wt, &rt, &dt);
		if (cc != 5) {
		    fprintf(stderr, "bad initial line %s\n", s);
		    goto err;
		}
#if 0
		printf("INITIAL %d %d %d %d %d\n", hf, fc, wt, rt, dt);
#endif

	    } else {

		/* Data line not understood
		 */
		fprintf(stderr, "Do not understand: %s\n", line);
		goto err;

	    }

	} else {

	    /* Exceptional line -
	     *   Interrupted
	     *   Abnormally long fsync(389642) took 8.364s
	     *   ...Other (fatal)
	     */
	    if (strncmp(line, "Interrupted", 11) == 0)
		NOTHING;
	    else if (strncmp(line, "Abnormally", 10) == 0)
	        NOTHING;
            else {	    
	        fprintf(stderr, "Exceptional line not understood: %s\n", line);
		goto err;
	    }

	}
    }
    r = 0;
err:
    free(line);
    free(cmd);
    free(poolid);
    free(tstamp);
    return r;
}
