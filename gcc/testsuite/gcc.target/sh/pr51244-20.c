/* Check that the SH specific sh_treg_combine RTL optimization pass works as
   expected.  On SH2A the expected insns are slightly different, see
   pr51244-21.c.  */
/* { dg-do compile { target "sh*-*-*" } } */
/* { dg-options "-O2" } */
/* { dg-skip-if "" { "sh*-*-*" } { "-m5*" "-m2a*" } { "" } } */
/* { dg-final { scan-assembler-times "tst" 6 } } */
/* { dg-final { scan-assembler-times "movt" 1 } } */
/* { dg-final { scan-assembler-times "cmp/eq" 2 } } */
/* { dg-final { scan-assembler-times "cmp/hi" 4 } } */
/* { dg-final { scan-assembler-times "cmp/gt" 2 } } */
/* { dg-final { scan-assembler-times "not\t" 1 } } */


/* non-SH2A: 2x tst, 1x movt, 2x cmp/eq, 1x cmp/hi
   SH2A: 1x tst, 1x nott, 2x cmp/eq, 1x cmp/hi  */
static inline int
blk_oversized_queue_0 (int* q)
{
  if (q[2])
    return q[1] == 5; 
  return (q[0] != 5);
}

int __attribute__ ((noinline))
get_request_0 (int* q, int rw)
{
  if (blk_oversized_queue_0 (q))
    {
      if ((rw == 1) || (rw == 0))
	return -33;
      return 0;
    }
  return -100;
}


/* 1x tst, 1x cmp/gt, 1x cmp/hi
   On SH2A mem loads/stores have a wrong length of 4 bytes and thus will
   not be placed in a delay slot.  This introduces an extra cmp/gt insn.  */
static inline int
blk_oversized_queue_1 (int* q)
{
  if (q[2])
    return q[1] > 5; 
  return (q[0] > 5);
}

int __attribute__ ((noinline))
get_request_1 (int* q, int rw)
{
  if (blk_oversized_queue_1 (q))
    {
      if ((rw == 1) || (rw == 0))
	return -33;
      return 0;
    }
  return -100;
}


/* 1x tst, 1x cmp/gt, 1x cmp/hi, 1x cmp/hi  */
static inline int
blk_oversized_queue_2 (int* q)
{
  if (q[2])
    return q[1] > 5; 
  return (q[0] < 5);
}

int __attribute__ ((noinline))
get_request_2 (int* q, int rw)
{
  if (blk_oversized_queue_2 (q))
    {
      if ((rw == 1) || (rw == 0))
	return -33;
      return 0;
    }
  return -100;
}


/* 2x tst, 1x cmp/hi, 1x not  */
static inline int
blk_oversized_queue_5 (int* q)
{
  if (q[2])
    return q[1] != 0; 
  return q[0] == 0;
}

int __attribute__ ((noinline))
get_request_5 (int* q, int rw)
{
  if (blk_oversized_queue_5 (q))
    {
      if ((rw == 1) || (rw == 0))
	return -33;
      return 0;
    }
  return -100;
}
