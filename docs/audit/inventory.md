# Инвентарь решений и состояния (`run_atv14`)

Сгенерировано по `afrd.c`: функция занимает строки 3480–5847 (2368 строк).

Локальных переменных в функции: 55 (без выведенных из конфига).

## Переменные состояния

| переменная | тип | установок | сбросов | чтений | где ставится | где сбрасывается |
|---|---|---|---|---|---|---|
| `measure_failed` | bool | 7 | 4 | 7 | 4573, 4587, 4738, 5146, 5216, 5380, 5626 | 4219, 4432, 4806, 5434 |
| `orig_sf_id` | int | 2 | 7 | 31 | 5108, 5594 | 4596, 5136, 5498, 5554, 5638, 5803, 5811 |
| `cadence_armed` | bool | 6 | 3 | 4 | 4628, 4656, 4705, 5365, 5678, 5741 | 4240, 4447, 4615 |
| `vinfo_state` | int | 2 | 6 | 3 | 4500, 4513 | 4253, 4456, 4488, 4572, 4586, 5691 |
| `env_growing` | bool | 1 | 6 | 2 | 4864 | 4262, 4356, 4446, 4873, 5223, 5309 |
| `fast_applied` | bool | 5 | 2 | 4 | 5054, 5078, 5092, 5114, 5144 | 4222, 4434 |
| `fast_class` | int | 2 | 5 | 3 | 5093, 5115 | 4237, 4437, 5055, 5079, 5145 |
| `cadence_heal_wait` | bool | 1 | 5 | 3 | 4694 | 4244, 4451, 4717, 4761, 5682 |
| `hdrinfo_armed` | bool | 3 | 3 | 2 | 4504, 4551, 5690 | 4251, 4454, 4469 |
| `vinfo_mid` | int | 2 | 4 | 10 | 4520, 4525 | 4255, 4458, 4527, 5693 |
| `held` | bool | 1 | 4 | 31 | 5109 | 4597, 5135, 5553, 5637 |
| `vinfo_confirm` | bool | 1 | 4 | 2 | 4501 | 4254, 4457, 4476, 5692 |
| `retries` | int | 0 | 4 | 7 |  | 4220, 4433, 4807, 5314 |
| `recheck_armed` | bool | 1 | 3 | 3 | 5668 | 3973, 4238, 4439 |
| `hold_keep` | bool | 1 | 3 | 2 | 5822 | 5564, 5784, 5814 |
| `counter_stall_logged` | bool | 1 | 3 | 2 | 4889 | 4271, 4461, 4866 |
| `cadence_checked` | bool | 1 | 3 | 3 | 4616 | 4241, 4448, 5679 |
| `cadence_heal_useless` | bool | 1 | 3 | 3 | 4718 | 4245, 4452, 5683 |
| `retry_gave_up` | bool | 2 | 2 | 3 | 4578, 4589 | 4221, 4459 |
| `video` | bool | 1 | 2 | 9 | 4112 | 4069, 4352 |
| `hint_seen` | mstime_t | 3 | 0 | 2 | 4851, 5427, 5433 |  |
| `pause_logged` | bool | 1 | 2 | 2 | 4306 | 4154, 4312 |
| `cadence_confirm` | bool | 1 | 2 | 2 | 4653 | 4247, 4760 |
| `cadence_reverted` | bool | 1 | 2 | 2 | 4734 | 4246, 4453 |
| `bump_probe_done` | bool | 2 | 1 | 3 | 4952, 5020 | 4248 |
| `cadence_expect_ms` | int | 3 | 0 | 2 | 4925, 5008, 5355 |  |
| `preferred_id` | int | 3 | 0 | 9 | 3722, 4031, 5469 |  |
| `pkg_probed` | bool | 1 | 2 | 2 | 4829 | 4258, 4442 |
| `video_corr` | double | 3 | 0 | 6 | 4259, 4443, 4833 |  |
| `fast_mismatch_logged` | bool | 1 | 2 | 2 | 5255 | 4223, 4435 |
| `fast_veto_logged` | bool | 1 | 2 | 2 | 5058 | 4224, 4436 |
| `crosscheck_logged` | bool | 1 | 1 | 2 | 4669 | 4462 |
| `skip_logged` | bool | 1 | 1 | 2 | 5484 | 5504 |
| `cadence_fixes` | int | 0 | 2 | 5 |  | 4243, 4450 |
| `bump_probe_veto_logged` | bool | 1 | 1 | 2 | 4988 | 4250 |
| `have_prev_frames` | bool | 1 | 1 | 4 | 4292 | 4370 |
| `paused` | bool | 1 | 1 | 6 | 4290 | 4369 |
| `still` | int | 0 | 2 | 3 |  | 4281, 4368 |
| `reconf` | bool | 2 | 0 | 7 | 3990, 3999 |  |
| `skip_pkg` | bool | 1 | 0 | 3 | 4418 |  |
| `vdec_fails` | int | 0 | 1 | 3 |  | 4074 |
| `bump_probe_ms` | int | 0 | 1 | 5 |  | 3861 |
| `recheck_timer` | mstime_t | 0 | 0 | 6 |  |  |
| `cadence_timer` | mstime_t | 0 | 0 | 10 |  |  |
| `hdrinfo_timer` | mstime_t | 0 | 0 | 8 |  |  |
| `cur_hz` | int | 0 | 0 | 12 |  |  |
| `ticks` | int | 0 | 0 | 4 |  |  |
| `session` | bool | 0 | 0 | 2 |  |  |
| `env_max_ms` | int | 0 | 0 | 4 |  |  |
| `env_sub_ms` | int | 0 | 0 | 3 |  |  |
| `env_skip_ms` | int | 0 | 0 | 3 |  |  |
| `env_early_ms` | int | 0 | 0 | 2 |  |  |
| `start_deb` | int | 0 | 0 | 2 |  |  |
| `stop_deb` | int | 0 | 0 | 3 |  |  |
| `correct_enable` | int | 0 | 0 | 5 |  |  |

## Точки применения режима

- `1606`: if (atv14_apply_mode (plat, bump, "bump до максимума") != 0)
- `2546`: rc = atv14_apply_mode (plat, sf_id, skipped);
- `3283`: if (atv14_apply_mode (plat, id, "восстановление режима прошлого запуска") == 0)
- `4538`: atv14_apply_mode (plat, vinfo_mid,
- `4540`: int rc = atv14_apply_mode (plat, cur_sf_id,
- `4697`: if (atv14_apply_mode (plat, back, "лечение подачи: возврат") == 0)
- `4700`: if (atv14_apply_mode (plat, want_sf_id, "лечение подачи: повтор") == 0)
- `4732`: (atv14_apply_mode (plat, back, "возврат: зажим не лечится") == 0))
- `5112`: if (atv14_apply_mode (plat, did, "быстрое решение") == 0)
- `5603`: int apply_rc = atv14_apply_mode (plat, want_sf_id, "переключение пропущено");
- `5729`: if (atv14_apply_mode (plat, want_sf_id, "переприменение") == 0)

## Точки возврата режима

- `2781`: if (atv14_restore_mode (plat, *orig_sf_id,
- `3465`: int rc = atv14_restore_mode (plat, *orig_sf_id,
- `4592`: (atv14_restore_mode (plat, orig_sf_id,
- `5493`: int rc = atv14_restore_mode (plat, orig_sf_id,
- `5797`: int rc = atv14_restore_mode (plat, orig_sf_id,
- `5840`: atv14_restore_mode (plat, orig_sf_id, "возврат пропущен, флаг backdoor снят");

## Решения guard

- `5566`: act = guard_decide (video_eff, want_sf_id, cur_sf_id, &orig_sf_id, &held);

## Применение формата

- `773`: colorspace_restore ();
- `2450`: colorspace_apply (nm);
- `2550`: colorspace_restore ();
- `4607`: colorspace_apply_current_mode ();
- `4639`: colorspace_apply_current_mode ();

## Потолок и burst

- `1424`: return (rc == 0) && ceiling_pkg_allows_pkg (pkg);
- `1570`: if (burst_running (&g_burst))
- `2879`: if (ceiling_build_restore (cmd, sizeof (cmd), had_override, prev) != 0)
- `2921`: if (ceiling_codec_probe_cmd (cmd, sizeof (cmd)) != 0)
- `2930`: const char *svc = ceiling_codec_pick (resp);
- `2938`: if (ceiling_build_codec_restart (cmd, sizeof (cmd), svc) != 0)
- `2966`: if (ceiling_density_parse (resp, &cur, &had, &next) != 0)
- `2982`: ceiling_restore (had, cur);	/* первый шаг мог примениться частично */
- `2988`: if (ceiling_restore (had, cur) != 0)
- `3002`: return ceiling_reset_codec ();
- `3005`: return ceiling_reset_density ();
- `3008`: if (ceiling_reset_codec () == 0)
- `3012`: return ceiling_reset_density ();
- `3056`: if (!ceiling_should_reset (g_ceiling_hz, hz))
- `3067`: g_ceiling_allowed = ceiling_pkg_allows (plat, pkg, sizeof (pkg)) ? 1 : 0;
- `3095`: if (ceiling_reset_now () == 0)
- `3351`: ms = burst_clamp_ms (cfg_get_int ("burst.ms", 0));
- `3352`: burst_req_t r = burst_request (&g_burst, ms, now);
- `3370`: g_burst.ms, burst_state_name (&g_burst));
- `3401`: state, g_burst.hz * 1000 / 256, burst_left_ms (&g_burst, now_us ()));
- `3410`: bool afrd_burst_oneshot (void)
- `3416`: bool afrd_burst_running (void)
- `3418`: return burst_running (&g_burst);
- `3421`: int afrd_burst_left_ms (void)
- `3423`: return burst_left_ms (&g_burst, now_us ());
- `3426`: const char *afrd_burst_state (void)
- `3428`: return burst_state_name (&g_burst);
- `3432`: int afrd_burst_ms (void)
- `3438`: int afrd_burst_hz (void)
- `3446`: void afrd_burst_self (int ms)
- `3449`: burst_request (&g_burst, ms, now);
- `3450`: burst_take_pending (&g_burst, now);
- `3474`: burst_notify (strcmp (why, "отмена") == 0 ? "cancelled" : "finished");
- `3475`: burst_finish (&g_burst);
- `3691`: if (g_burst_oneshot && !burst_running (&g_burst))
- `3697`: if ((plat->kind == PLAT_ATV14) && !plat->sf_backdoor && !burst_running (&g_burst))
- `3712`: if (burst_take_pending (&g_burst, now_us ()))
- `3715`: burst_notify ("started");
- `3745`: if (!g_enable && !burst_running (&g_burst))
- `3765`: if (burst_take_pending (&g_burst, now_us ()))
- `3768`: burst_notify ("started");
- `3797`: bool session = burst_running (&g_burst);
- `3968`: if (!burst_running (&g_burst) && burst_take_pending (&g_burst, now_us ()))
- `3975`: burst_notify ("started");
- `3982`: if (burst_running (&g_burst) &&
- `3983`: (burst_expired (&g_burst, now_us ()) || g_burst_video_changed ||
- `3984`: burst_cancel_requested (&g_burst)))
- `3986`: burst_session_end (plat, &orig_sf_id, &cur_sf_id, &held,
- `3987`: burst_cancel_requested (&g_burst) ? "отмена"
- `4086`: !ceiling_recent_within (CEILING_RESTART_WINDOW_US))
- `4096`: ceiling_recent_within (CEILING_RESTART_WINDOW_US);
- `4129`: bool own_restart = ceiling_take_restart (cur_sf_id) ||
- `4130`: ceiling_recent_within (CEILING_RESTART_WINDOW_US);
- `4135`: ceiling_forget ();
- `4195`: bool own_reset = ceiling_take_restart (cur_sf_id) ||
- `4196`: ceiling_recent_within (8000000ULL);
- `4201`: if (!own_reset && burst_running (&g_burst))
- `4215`: ceiling_forget ();
- `4624`: if (ceiling_recent_within (CEILING_RESTART_WINDOW_US))
- `4691`: !cadence_heal_useless && !burst_running (&g_burst))
- `4730`: (back != want_sf_id) && !burst_running (&g_burst) &&
- `5393`: if (burst_running (&g_burst))
- `5612`: if (burst_running (&g_burst))
- `5613`: burst_notify ("switched");
- `5701`: ceiling_pending_step (plat, cur_sf_id, video_eff);
- `5713`: ceiling_pending_step (plat, cur_sf_id, video_eff);
- `6304`: if (burst_running (&g_burst) && g_burst_self)

## Ключи конфига (69)

| ключ | путь | чтений в `afrd.c` |
|---|---|---|
| `enable` | CFGKEY_BOTH | 1 |
| `log.enable` | CFGKEY_BOTH | 1 |
| `log.file` | CFGKEY_BOTH | 1 |
| `mode.prefer.exact` | CFGKEY_BOTH | 1 |
| `mode.use.fract` | CFGKEY_BOTH | 2 |
| `mode.blacklist.rates` | CFGKEY_BOTH | 1 |
| `mode.min.hz.list` | CFGKEY_BOTH | 1 |
| `switch.skip.pkg` | CFGKEY_BOTH | 2 |
| `switch.timeout` | CFGKEY_BOTH | 2 |
| `switch.delay.off` | CFGKEY_BOTH | 2 |
| `switch.bump` | CFGKEY_ATV14 | 1 |
| `switch.bump.pkg` | CFGKEY_ATV14 | 3 |
| `switch.ceiling.reset` | CFGKEY_ATV14 | 1 |
| `switch.ceiling.method` | CFGKEY_ATV14 | 1 |
| `switch.ceiling.pkg` | CFGKEY_ATV14 | 2 |
| `switch.bump.probe.ms` | CFGKEY_ATV14 | 1 |
| `switch.revert` | CFGKEY_ATV14 | 2 |
| `fps.feed.ratio.pct` | CFGKEY_ATV14 | 1 |
| `fps.feed.gap.pct` | CFGKEY_ATV14 | 2 |
| `switch.bump.on.app` | CFGKEY_ATV14 | 1 |
| `fps.declare.fast` | CFGKEY_ATV14 | 2 |
| `vendor.disp.cli` | CFGKEY_ATV14 | 1 |
| `vendor.disp.persist` | CFGKEY_ATV14 | 1 |
| `vendor.disp.cap` | CFGKEY_ATV14 | 0 |
| `trigger.poll.ms` | CFGKEY_ATV14 | 1 |
| `trigger.start.debounce.ms` | CFGKEY_ATV14 | 1 |
| `trigger.stop.debounce.ms` | CFGKEY_ATV14 | 1 |
| `fps.measure.envelope.ms` | CFGKEY_ATV14 | 1 |
| `fps.measure.envelope.early.ms` | CFGKEY_ATV14 | 1 |
| `fps.measure.envelope.max.ms` | CFGKEY_ATV14 | 1 |
| `fps.measure.envelope.sub.ms` | CFGKEY_ATV14 | 1 |
| `fps.measure.envelope.skip.ms` | CFGKEY_ATV14 | 1 |
| `fps.measure.retries` | CFGKEY_ATV14 | 1 |
| `fps.declared.crosscheck.pct` | CFGKEY_ATV14 | 1 |
| `fps.recheck.delay.ms` | CFGKEY_ATV14 | 1 |
| `fps.cadence.fix.ms` | CFGKEY_ATV14 | 1 |
| `fps.prefer.api` | CFGKEY_ATV14 | 1 |
| `pause.keep` | CFGKEY_ATV14 | 1 |
| `pause.mode` | CFGKEY_ATV14 | 1 |
| `burst.ms` | CFGKEY_ATV14 | 1 |
| `burst.broadcast` | CFGKEY_ATV14 | 1 |
| `switch.restore.on.stop` | CFGKEY_ATV14 | 1 |
| `fps.correct.enable` | CFGKEY_ATV14 | 1 |
| `fps.correct.list` | CFGKEY_ATV14 | 1 |
| `fps.correct.av1` | CFGKEY_ATV14 | 1 |
| `fps.correct.av1.offset` | CFGKEY_ATV14 | 2 |
| `fps.correct.av1.pct` | CFGKEY_ATV14 | 1 |
| `fps.correct.av1.pkg` | CFGKEY_ATV14 | 1 |
| `mode.extra` | CFGKEY_LEGACY | 0 |
| `mode.path` | CFGKEY_LEGACY | 1 |
| `vdec.sysfs` | CFGKEY_LEGACY | 1 |
| `vdec.blacklist` | CFGKEY_LEGACY | 1 |
| `frhint.vdec.blacklist` | CFGKEY_LEGACY | 1 |
| `hdmi.sysfs` | CFGKEY_LEGACY | 1 |
| `hdmi.state` | CFGKEY_LEGACY | 1 |
| `switch.delay.on` | CFGKEY_LEGACY | 1 |
| `switch.delay.retry` | CFGKEY_LEGACY | 1 |
| `switch.blackout` | CFGKEY_LEGACY | 1 |
| `switch.ignore` | CFGKEY_LEGACY | 1 |
| `switch.hdmi` | CFGKEY_LEGACY | 1 |
| `cs.select` | CFGKEY_BOTH | 0 |
| `cs.restore` | CFGKEY_BOTH | 0 |
| `cs.list.path` | CFGKEY_BOTH | 0 |
| `cs.path` | CFGKEY_BOTH | 0 |
| `uevent.filter.frhint` | CFGKEY_LEGACY | 1 |
| `uevent.filter.vdec` | CFGKEY_LEGACY | 1 |
| `uevent.filter.hdmi` | CFGKEY_LEGACY | 1 |
| `uevent.filter.hdcp` | CFGKEY_LEGACY | 1 |
| `toast.hz` | CFGKEY_APP | 0 |
